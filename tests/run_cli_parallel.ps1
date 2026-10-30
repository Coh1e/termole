param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$HelperA,
  [Parameter(Mandatory = $true)][string]$HelperB,
  [Parameter(Mandatory = $true)][string]$CwdA,
  [Parameter(Mandatory = $true)][string]$CwdB,
  [Parameter(Mandatory = $true)][string]$ReportA,
  [Parameter(Mandatory = $true)][string]$ReportB
)

function Launch-Termole([string]$Helper, [string]$Cwd, [string]$Report, [string]$Value) {
  $line = "--cwd `"$Cwd`" --env `"TERMOLE_PROBE_REPORT=$Report`" --env `"TERMOLE_PROBE_VALUE=$Value`" -- `"$Helper`" `"$Value`" `"a b`""
  Start-Process -FilePath $Exe -ArgumentList $line -PassThru -WindowStyle Hidden
}

$first = Launch-Termole $HelperA $CwdA $ReportA 'parallel A'
$second = Launch-Termole $HelperB $CwdB $ReportB 'parallel B'

$firstExited = $first.WaitForExit(15000)
$secondExited = $second.WaitForExit(15000)
if (-not $firstExited) { Stop-Process -Id $first.Id -Force }
if (-not $secondExited) { Stop-Process -Id $second.Id -Force }
if (-not $firstExited -or -not $secondExited) {
  Write-Error "parallel Termole instances did not exit"
  exit 1
}
if ($first.ExitCode -ne 0 -or $second.ExitCode -ne 0) {
  Write-Error "parallel Termole instances failed: $($first.ExitCode), $($second.ExitCode)"
  exit 1
}
exit 0
