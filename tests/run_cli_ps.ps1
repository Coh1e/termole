param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Helper,
  [Parameter(Mandatory = $true)][string]$Cwd,
  [Parameter(Mandatory = $true)][string]$Report
)

# A native console program is waited for by PowerShell, so -e alone makes the
# pipeline sequential: this asserts the launcher forwards the program status.
& $Exe --cwd $Cwd --env "TERMOLE_PROBE_REPORT=$Report" --env "TERMOLE_PROBE_VALUE=ps value" `
  -e $Helper alpha "a b"
if ($LASTEXITCODE -ne 0) {
  Write-Error "PowerShell launch exited with $LASTEXITCODE"
  exit $LASTEXITCODE
}
exit 0
