param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Helper
)

$OutputEncoding = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# The result must already be complete when the pipeline expression returns,
# which only works because termole.exe is a console launcher that waits.
$lines = @('alpha', 'beta')
$result = $lines | & $Exe --stdio $Helper --copy-stdin
if ($LASTEXITCODE -ne 0) {
  Write-Error "PowerShell --stdio pipeline exited with $LASTEXITCODE"
  exit 1
}
$joined = ($result -join "`n")
if ($joined -ne ($lines -join "`n")) {
  Write-Error "PowerShell --stdio result mismatch: '$joined'"
  exit 1
}

# The program's status is the launcher's status.
$null = $lines | & $Exe --stdio $Helper --copy-stdin --exit-code 7
if ($LASTEXITCODE -ne 7) {
  Write-Error "PowerShell --stdio did not forward exit code 7 (got $LASTEXITCODE)"
  exit 1
}

# A larger real pipeline: every line must survive and stay in order.
$many = 1..8000 | ForEach-Object { "line-$_" }
$all = $many | & $Exe --stdio $Helper --copy-stdin
if ($LASTEXITCODE -ne 0) {
  Write-Error "large PowerShell --stdio pipeline exited with $LASTEXITCODE"
  exit 1
}
if (($all | Measure-Object).Count -ne 8000) {
  Write-Error "large PowerShell --stdio pipeline lost lines (got $(($all | Measure-Object).Count))"
  exit 1
}
if ($all[0] -ne 'line-1' -or $all[7999] -ne 'line-8000') {
  Write-Error "large PowerShell --stdio pipeline changed the order"
  exit 1
}

# The caller closes the result pipe early: the pipeline must end instead of
# blocking on the remaining output.
$big = 1..20000 | ForEach-Object { "line-$_" }
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$first = $big | & $Exe --stdio $Helper --copy-stdin | Select-Object -First 1
if ($stopwatch.Elapsed.TotalSeconds -gt 20) {
  Write-Error "early-closed --stdio pipeline did not finish"
  exit 1
}
if ($first -ne 'line-1') {
  Write-Error "early-closed --stdio pipeline lost ordering: '$first'"
  exit 1
}
exit 0
