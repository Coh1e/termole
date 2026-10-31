<#
.SYNOPSIS
  Print the Termole ticket frontier: which tickets are ready, which are blocked,
  and whether docs/issue-index.md still matches the tracker.

.DESCRIPTION
  The tracker is the source of truth for state; docs/issue-index.md is the cache
  for blocking edges. This script joins the two so "what can I start?" is one
  command instead of a manual sweep of 30 issues.

  Exit code 1 when the index points at an issue the tracker does not have
  (DRIFT). Blocked/ready counts never fail the run.

.PARAMETER All
  Also list closed tickets.

.EXAMPLE
  scripts\issue-status.ps1
  scripts\issue-status.ps1 -All
#>
[CmdletBinding()]
param(
    [string] $Repo = 'Coh1e/termole',
    [string] $Index = '',
    [switch] $All
)

$ErrorActionPreference = 'Stop'

if (-not $Index) {
    $Index = Join-Path $PSScriptRoot '..\docs\issue-index.md'
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Error 'gh is required (see docs/agents/issue-tracker.md).'
    exit 2
}

if (-not (Test-Path $Index)) {
    Write-Error "issue index not found: $Index"
    exit 2
}

# number -> 'OPEN' / 'CLOSED'
$states = @{}
$titles = @{}
$json = gh issue list --repo $Repo --state all --limit 200 --json number,state,title
foreach ($issue in ($json | ConvertFrom-Json)) {
    $states[[int] $issue.number] = [string] $issue.state
    $titles[[int] $issue.number] = [string] $issue.title
}

function Get-TicketRows {
    param([string] $Path)
    foreach ($line in (Get-Content -Path $Path)) {
        if ($line -notmatch '^\|\s*(T\d+)\s*\|\s*\[#(\d+)') { continue }
        $ticket = $Matches[1]
        $number = [int] $Matches[2]
        $blockers = @()
        foreach ($match in [regex]::Matches($line, '#(\d+)')) {
            $candidate = [int] $match.Groups[1].Value
            if ($candidate -ne $number -and $blockers -notcontains $candidate) {
                $blockers += $candidate
            }
        }
        [pscustomobject]@{ Ticket = $ticket; Number = $number; Blockers = $blockers }
    }
}

function Get-OpenBlockers {
    param($Row)
    @($Row.Blockers | Where-Object {
        -not $states.ContainsKey($_) -or $states[$_] -ne 'CLOSED'
    })
}

function Format-Row {
    param($Row, [string] $Kind)
    $waiting = @(Get-OpenBlockers -Row $Row | ForEach-Object { "#$_" })
    $name = $titles[$Row.Number]
    if (-not $name) { $name = '(missing from tracker)' }
    $suffix = ''
    if ($Kind -eq 'BLOCKED' -and $waiting.Count -gt 0) {
        $suffix = '  <- waiting on ' + ($waiting -join ', ')
    }
    "{0,-8} #{1,-3} {2,-4} {3}{4}" -f $Kind, $Row.Number, $Row.Ticket, $name, $suffix
}

$rows = @(Get-TicketRows -Path $Index)
if ($rows.Count -eq 0) {
    Write-Error "no ticket rows parsed from $Index"
    exit 2
}

$ready = @()
$blocked = @()
$closed = @()
$drift = @()

foreach ($row in $rows) {
    if (-not $states.ContainsKey($row.Number)) {
        $drift += "#$($row.Number) $($row.Ticket) (index row has no tracker issue)"
        continue
    }
    if ($states[$row.Number] -eq 'CLOSED') {
        $closed += $row
        continue
    }
    if (@(Get-OpenBlockers -Row $row).Count -eq 0) { $ready += $row } else { $blocked += $row }
}

Write-Output "Termole frontier ($Repo)"
Write-Output ''

foreach ($row in ($ready | Sort-Object Number)) { Write-Output (Format-Row -Row $row -Kind 'READY') }
foreach ($row in ($blocked | Sort-Object Number)) { Write-Output (Format-Row -Row $row -Kind 'BLOCKED') }

if ($All) {
    foreach ($row in ($closed | Sort-Object Number)) { Write-Output (Format-Row -Row $row -Kind 'CLOSED') }
}

Write-Output ''
Write-Output ("ready {0}  blocked {1}  closed {2}" -f $ready.Count, $blocked.Count, $closed.Count)

if ($drift.Count -gt 0) {
    Write-Output ''
    foreach ($item in $drift) { Write-Output "DRIFT    $item" }
    exit 1
}

exit 0
