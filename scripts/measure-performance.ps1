<#
.SYNOPSIS
  Record T21 launcher/GUI resource and startup baselines using public entries.
.DESCRIPTION
  Run on an idle interactive desktop after building the Windows Release preset. First launch is recorded
  separately; filesystem/standby caches are not purged. No global state changes.
#>
[CmdletBinding()]
param(
    [string] $Build = "$PSScriptRoot/../.local/build/nmake-release",
    [string] $Output = "$PSScriptRoot/../.local/t21-performance",
    [ValidateSet('normal', 'execute', 'throughput')]
    [string[]] $Kinds = @('normal', 'execute', 'throughput'),
    [ValidateRange(1, 100)]
    [int] $Iterations = 5
)
$ErrorActionPreference = 'Stop'
$Build = (Resolve-Path $Build).Path
$app = Join-Path $Build 'bin/termole.exe'
$helper = Join-Path $Build 'bin/cli-report-helper.exe'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path $Output).Path
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class T21Window {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("kernel32.dll")]
    public static extern bool GetExitCodeProcess(IntPtr process, out uint code);
}
'@
$results = @()
function Measure-Run([string] $Kind, [int] $Iteration) {
    $title = "T21-$PID-$Kind-$Iteration"
    $pidFile = Join-Path $Output "$Kind-$Iteration.pid"
    if (Test-Path -LiteralPath $pidFile) { Remove-Item -LiteralPath $pidFile }
    $arguments = @('--no-config', '--close-on-exit', '--title', $title)
    if ($Kind -ne 'normal') { $arguments += '-e' } else { $arguments += '--' }
    $arguments += @(('"' + $helper + '"'), '--write-pid', ('"' + $pidFile + '"'))
    if ($Kind -eq 'throughput') {
        # Holds permit GUI peak/working-set sampling before and after output.
        $arguments += @('--sleep-ms', '1000', '--linger-ms', '1000', '--lines', '50000')
    } else {
        $arguments += @('--sleep-ms', '8000')
    }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $launcher = Start-Process -FilePath $app -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $launcherHandle = $launcher.Handle
    $window = [IntPtr]::Zero
    $gui = $null
    try {
        while ($clock.ElapsedMilliseconds -lt 15000) {
            $window = [T21Window]::FindWindow('TermoleStandaloneWindow', $title)
            if ($window -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 5
        }
        if ($window -eq [IntPtr]::Zero) { throw "GUI did not appear: $title" }
        [uint32] $guiId = 0
        [void][T21Window]::GetWindowThreadProcessId($window, [ref] $guiId)
        $gui = [Diagnostics.Process]::GetProcessById($guiId)
        $guiHandle = $gui.Handle
        $windowMs = $clock.Elapsed.TotalMilliseconds
        while (!(Test-Path -LiteralPath $pidFile) -and $clock.ElapsedMilliseconds -lt 15000) {
            Start-Sleep -Milliseconds 5
        }
        if (!(Test-Path -LiteralPath $pidFile)) { throw 'Target never reported readiness' }
        $targetReadyMs = $clock.Elapsed.TotalMilliseconds
        $launcher.Refresh()
        $normalReturned = $launcher.HasExited
        if ($Kind -eq 'normal' -and !$normalReturned) { throw 'Normal launcher did not return' }
        if ($Kind -ne 'throughput') { Start-Sleep -Milliseconds 1000 }
        $gui.Refresh()
        $idleCpuStart = $gui.TotalProcessorTime.TotalMilliseconds
        $idleStart = $clock.Elapsed.TotalMilliseconds
        $workingSet = $gui.WorkingSet64
        $peak = $gui.PeakWorkingSet64
        while (!$gui.HasExited -and $clock.ElapsedMilliseconds -lt 30000) {
            $gui.Refresh()
            if ($gui.HasExited) { break }
            $peak = [Math]::Max($peak, $gui.PeakWorkingSet64)
            if ($Kind -ne 'throughput' -and $clock.Elapsed.TotalMilliseconds - $idleStart -ge 5000) { break }
            Start-Sleep -Milliseconds 10
        }
        $idleCpu = $null
        $idleWall = $null
        if ($Kind -ne 'throughput') {
            $gui.Refresh()
            if ($gui.HasExited) { throw 'GUI ended before idle sample' }
            $workingSet = $gui.WorkingSet64
            $idleCpu = $gui.TotalProcessorTime.TotalMilliseconds - $idleCpuStart
            $idleWall = $clock.Elapsed.TotalMilliseconds - $idleStart
        }
        if (!$gui.WaitForExit(15000)) { throw 'GUI did not finish' }
        $wall = $clock.Elapsed.TotalMilliseconds
        if (!$launcher.WaitForExit(5000)) { throw 'Launcher did not finish' }
        [uint32] $launcherCode = 0
        [uint32] $guiCode = 0
        if (![T21Window]::GetExitCodeProcess($launcherHandle, [ref] $launcherCode) -or
            ![T21Window]::GetExitCodeProcess($guiHandle, [ref] $guiCode)) { throw 'Exit code query failed' }
        if ($launcherCode -ne 0 -or $guiCode -ne 0) {
            throw "Launcher or GUI failed: launcher=$launcherCode, GUI=$guiCode"
        }
        [pscustomobject]@{
            kind=$Kind; iteration=$Iteration; gui_pid=$guiId
            window_discovered_ms=$windowMs; target_pid_observed_ms=$targetReadyMs
            normal_launcher_returned_before_target_exit=$normalReturned
            idle_cpu_ms=$idleCpu; idle_wall_ms=$idleWall
            working_set_bytes=$workingSet; peak_working_set_bytes=$peak
            launch_to_gui_exit_ms=$wall
            launcher_exit_code=$launcherCode; gui_exit_code=$guiCode
            managed_launcher_exit_code=$launcher.ExitCode; managed_gui_exit_code=$gui.ExitCode
            lines=$(if ($Kind -eq 'throughput') { 50000 } else { 0 })
        }
    } finally {
        if ($gui -and !$gui.HasExited -and $window -ne [IntPtr]::Zero) {
            [void][T21Window]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
            [void]$gui.WaitForExit(15000)
        }
        if ($gui) { $gui.Dispose() }
        $launcher.Dispose()
    }
}
foreach ($kind in $Kinds) {
    foreach ($iteration in 1..$Iterations) {
        $row = Measure-Run $kind $iteration
        $results += $row
        $row | Format-List | Out-String | Write-Host
        $results | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $Output 'measurements.json')
    }
}
[pscustomobject]@{
    measured_at=(Get-Date -Format o)
    os=[Environment]::OSVersion.VersionString
    processor=$env:PROCESSOR_IDENTIFIER
    logical_processors=[Environment]::ProcessorCount
    cache_policy='First launch and subsequent fresh processes; OS file cache uncontrolled'
    app_sha256=(Get-FileHash $app).Hash
    dll_sha256=(Get-FileHash (Join-Path $Build 'bin/termole.dll')).Hash
} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $Output 'environment.json')
