param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Root)
$ErrorActionPreference = 'Stop'
$Root = [IO.Directory]::CreateDirectory((Join-Path $Root ([guid]::NewGuid().ToString('N')))).FullName
Write-Host "Artifacts: $Root"
$entry = (Get-Item "$Package/share/termole/examples/select-file.ps1").FullName
$exe = (Get-Item "$Package/bin/termole.exe").FullName
$tempDir = [IO.Directory]::CreateDirectory("$Root/temp").FullName
Add-Type @"
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
public static class PickerLifecycle {
    [DllImport("user32")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("kernel32")] public static extern void GetSystemTimePreciseAsFileTime(out long time);
    [DllImport("user32", SetLastError=true)] public static extern bool PostMessage(IntPtr window, uint message, IntPtr key, IntPtr flags);
    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInfo { public IntPtr reserved, peb, reserved2, reserved3, id, parent; }
    [DllImport("ntdll")] static extern int NtQueryInformationProcess(IntPtr handle, int infoClass, out ProcessInfo info, int size, out int returned);
    public static int Parent(Process p) {
        ProcessInfo info;
        int returned;
        return NtQueryInformationProcess(p.Handle, 0, out info, Marshal.SizeOf(typeof(ProcessInfo)), out returned) == 0 ? info.parent.ToInt32() : 0;
    }
}
"@
[void][PickerLifecycle]::SetThreadDpiAwarenessContext(-4)
function Find-Child($name, $ancestor) {
    foreach ($p in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
        try {
            $parent = [PickerLifecycle]::Parent($p)
            while ($parent -gt 0) {
                if ($parent -eq $ancestor) { return $p }
                $owner = [Diagnostics.Process]::GetProcessById($parent)
                try { $parent = [PickerLifecycle]::Parent($owner) } finally { $owner.Dispose() }
            }
        } catch { }
    }
}
function Start-Picker($label) {
    $dir = [IO.Directory]::CreateDirectory("$Root/$label").FullName
    foreach ($suffix in 'alpha','beta') {
        [IO.File]::WriteAllText("$dir/$label-$suffix.txt", "$label $suffix preview")
    }
    $start = New-Object Diagnostics.ProcessStartInfo powershell.exe
    $start.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $entry + '" -Directory "' + $dir + '"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.EnvironmentVariables.Remove("FZF_DEFAULT_OPTS")
    $start.EnvironmentVariables.Remove("FZF_DEFAULT_OPTS_FILE")
    $start.EnvironmentVariables["APPDATA"] = "$Root/profile"
    $start.EnvironmentVariables["TEMP"] = $tempDir
    $start.EnvironmentVariables["TMP"] = $tempDir
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $proc = [Diagnostics.Process]::Start($start)
    $script:started += $proc
    for ($i = 0; $i -lt 200 -and !$proc.HasExited; $i++) {
        foreach ($gui in @(Get-Process termole -ErrorAction SilentlyContinue)) {
            if ($gui.Path -ne $exe -or !$gui.MainWindowHandle.ToInt64()) { continue }
            $parent = [PickerLifecycle]::Parent($gui)
            if ([PickerLifecycle]::Parent([Diagnostics.Process]::GetProcessById($parent)) -ne $proc.Id) { continue }
            $view = [PickerLifecycle]::FindWindowEx($gui.MainWindowHandle,0,'TermoleViewWindow',$null)
            $fzf = Find-Child fzf $proc.Id
            if ($fzf -and $view.ToInt64()) {
                return @{Proc=$proc; Fzf=$fzf; Gui=$gui; View=$view; Dir=$dir}
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Missing $label picker window"
}
function Send-Message($window, $msg, $key) {
    if (![PickerLifecycle]::PostMessage($window,$msg,$key,0)) { throw "Message $msg failed" }
}
function Save-View($picker, $name) {
    & "$PSScriptRoot/../../scripts/capture-window.ps1" -Hwnd $picker.View.ToInt64() -OutputPath "$Root/$name.png" -StatsPath "$Root/$name.txt" -Quiet
}
function Check-Result($picker, $status, $output) {
    $proc = $picker.Proc
    if (!$proc.WaitForExit(15000)) { throw "Picker timed out" }
    $out = [IO.MemoryStream]::new()
    $proc.StandardOutput.BaseStream.CopyTo($out)
    $err = [IO.MemoryStream]::new()
    $proc.StandardError.BaseStream.CopyTo($err)
    $expected = [Text.Encoding]::UTF8.GetBytes([string]$output)
    if ($proc.ExitCode -ne $status -or $err.Length -or [Convert]::ToBase64String($out.ToArray()) -cne [Convert]::ToBase64String($expected)) {
        throw "exit=$($proc.ExitCode) stdout=$([Text.Encoding]::UTF8.GetString($out.ToArray())) stderr=$([Text.Encoding]::UTF8.GetString($err.ToArray()))"
    }
    Write-Host "picker=$($proc.Id) exit=$status stdoutBytes=$($out.Length) stderrBytes=$($err.Length) stdout=$output"
}
function Cancel-Picker($picker,$mode) {
    Start-Sleep -Seconds 2
    Save-View $picker $mode
    # Change selection to start a fresh invocation of the packaged preview.
    Send-Message $picker.View 256 38
    for ($i = 0; $i -lt 150; $i++) {
        $preview = Find-Child powershell $picker.Fzf.Id
        if ($preview -and !$preview.HasExited) { break }
        Start-Sleep -Milliseconds 10
    }
    if (!$preview -or $preview.HasExited) { throw 'No running fzf preview before cancel' }
    $window = $picker.View
    $message = 256
    $key = 27
    if ($mode -eq 'close') {
        $window = $picker.Gui.MainWindowHandle
        $message = 16
        $key = 0
    }
    [long]$dispatch = 0
    [PickerLifecycle]::GetSystemTimePreciseAsFileTime([ref]$dispatch)
    if (![PickerLifecycle]::PostMessage($window,$message,$key,0)) {
        throw "Cancel PostMessage failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    Write-Host "$mode cancel dispatch UTC=$([DateTime]::FromFileTimeUtc($dispatch).ToString('O'))"
    Check-Result $picker 130 ''
    foreach ($child in $preview,$picker.Fzf) {
        if (!$child.HasExited) { throw "Child $($child.Id) still running" }
        $exitTime = $child.ExitTime.ToFileTimeUtc()
        if ($exitTime -lt $dispatch -or $exitTime -gt $picker.Proc.ExitTime.ToFileTimeUtc()) {
            throw "Child $($child.Id) exited outside cancellation interval"
        }
        Write-Host "$mode $($child.ProcessName) PID=$($child.Id) exit=$($child.ExitTime.ToString('O'))"
    }
    Write-Host "$mode example PID=$($picker.Proc.Id) exit=$($picker.Proc.ExitTime.ToString('O'))"
}
$started = @()
try {
    $first = Start-Picker first
    $second = Start-Picker second
    Cancel-Picker $first escape
    foreach ($c in 'beta'.ToCharArray()) { Send-Message $second.View 258 ([int]$c) }
    Start-Sleep -Seconds 3
    Save-View $second survivor
    Send-Message $second.View 256 13
    Check-Result $second 0 ((Join-Path $second.Dir 'second-beta.txt') + [Environment]::NewLine)
    $third = Start-Picker third
    Cancel-Picker $third close
} finally {
    foreach ($p in $started) {
        if (!$p.HasExited) { & taskkill.exe /PID $p.Id /T /F | Out-Null }
        $p.Dispose()
    }
}
