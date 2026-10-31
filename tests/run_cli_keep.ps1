param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Helper,
  [Parameter(Mandatory = $true)][string]$Report
)

$line = "--gui --env `"TERMOLE_PROBE_REPORT=$Report`" -- `"$Helper`" --exit-code 7"
$process = Start-Process -FilePath $Exe -ArgumentList $line -PassThru
$deadline = (Get-Date).AddSeconds(10)
while (-not (Test-Path $Report) -and (Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 100
}
if (-not (Test-Path $Report)) {
  if (-not $process.HasExited) { $process.Kill() }
  Write-Error "keep-window target did not produce a report"
  exit 1
}
Start-Sleep -Milliseconds 500
if ($process.HasExited) {
  Write-Error "a failed child closed the window despite the default keep policy"
  exit 1
}
if (-not $process.CloseMainWindow()) {
  $process.Kill()
  Write-Error "could not close the retained window"
  exit 1
}
if (-not $process.WaitForExit(10000)) {
  $process.Kill()
  Write-Error "retained window did not close"
  exit 1
}
if ($process.ExitCode -ne 7) {
  Write-Error "retained app exit code was $($process.ExitCode), expected 7"
  exit 1
}
exit 0
