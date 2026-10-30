param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Helper,
  [Parameter(Mandatory = $true)][string]$Cwd,
  [Parameter(Mandatory = $true)][string]$Report
)

$line = "--cwd `"$Cwd`" --env `"TERMOLE_PROBE_REPORT=$Report`" --env `"TERMOLE_PROBE_VALUE=ps value`" -- `"$Helper`" `"alpha`" `"a b`""
$process = Start-Process -FilePath $Exe -ArgumentList $line -Wait -PassThru -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
  Write-Error "PowerShell launch exited with $($process.ExitCode)"
  exit $process.ExitCode
}
exit 0
