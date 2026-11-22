param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Root)
$ErrorActionPreference = 'Stop'
$Root = [IO.Directory]::CreateDirectory($Root).FullName
$folder = [IO.Directory]::CreateDirectory((Join-Path $Root 'files')).FullName
# Each invocation owns the only processes that can launch from this package path.
$isolated = Join-Path $Root ([Guid]::NewGuid().ToString())
$bin = [IO.Directory]::CreateDirectory((Join-Path $isolated 'bin')).FullName
$examples = [IO.Directory]::CreateDirectory((Join-Path $isolated 'share/termole/examples')).FullName
Copy-Item -LiteralPath "$Package/bin/termole.exe" -Destination $bin
foreach ($script in 'select-file.ps1','preview-file.ps1') {
    Copy-Item -LiteralPath "$Package/share/termole/examples/$script" -Destination $examples
}
$exe = Join-Path $bin 'termole.exe'
$entry = Join-Path $examples 'select-file.ps1'
[IO.File]::WriteAllText("$folder/alpha.txt", "ALPHA PREVIEW`n" * 1600)
[IO.File]::WriteAllBytes("$folder/binary.bin", [byte[]](0,65,27))
[IO.File]::WriteAllText("$folder/locked.txt", "never displayed")
$name = "target %PATH% & [1].txt"
[IO.File]::WriteAllText((Join-Path $folder $name), "CURRENT PREVIEW")
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PickerDriver {
[DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr mode);
[DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp);
}
"@
$previousDpi = [PickerDriver]::SetThreadDpiAwarenessContext(-4)
$locked = [IO.File]::Open("$folder/locked.txt", 'Open', 'ReadWrite', 'None')
try {
    foreach ($case in @(
        @{Query='alpha'; Name='alpha.txt'},
        @{Query='binary'; Name='binary.bin'},
        @{Query='locked'; Name='locked.txt'},
        @{Query='target'; Name=$name}
    )) {
        $start = New-Object Diagnostics.ProcessStartInfo powershell.exe
        $start.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $entry + '" -Directory "' + $folder + '"'
        $start.UseShellExecute = $false
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $start.StandardOutputEncoding = [Text.Encoding]::UTF8
        $start.EnvironmentVariables['TEMP'] = [IO.Directory]::CreateDirectory((Join-Path $Root 'temp')).FullName
        $start.EnvironmentVariables['TMP'] = $start.EnvironmentVariables['TEMP']
        $start.EnvironmentVariables['APPDATA'] = Join-Path $Root 'profile'
        $gui = $null
        $proc = [Diagnostics.Process]::Start($start)
        $view = [IntPtr]::Zero
        try {
            for ($i = 0; $i -lt 200 -and !$proc.HasExited; $i++) {
                $gui = Get-Process termole -ErrorAction SilentlyContinue | Where-Object {
                    $_.Path -eq $exe -and $_.MainWindowHandle -ne [IntPtr]::Zero
                } | Select-Object -First 1
                if ($gui) {
                    $view = [PickerDriver]::FindWindowEx($gui.MainWindowHandle, 0, 'TermoleViewWindow', $null)
                    if ($view -ne [IntPtr]::Zero) { break }
                }
                Start-Sleep -Milliseconds 100
            }
            if ($view -eq [IntPtr]::Zero) {
                if ($proc.HasExited) { throw "Picker exited $($proc.ExitCode): $($proc.StandardError.ReadToEnd())" }
                throw 'Picker did not open'
            }
            Start-Sleep -Seconds 2
            foreach ($key in @(38,38,40,38,40)) {
                [void][PickerDriver]::PostMessage($view,256,$key,0)
            }
            foreach ($ch in $case.Query.ToCharArray()) {
                [void][PickerDriver]::PostMessage($view,258,[int]$ch,0)
            }
            Start-Sleep -Seconds 3
            $png = Join-Path $Root ($case.Query + '.png')
            & "$PSScriptRoot/../../scripts/capture-window.ps1" -Hwnd $view.ToInt64() -OutputPath $png -StatsPath "$png.txt" -Quiet
            [void][PickerDriver]::PostMessage($view,256,13,0)
            if (!$proc.WaitForExit(15000)) { throw 'Enter timed out' }
            $out = $proc.StandardOutput.ReadToEnd()
            $err = $proc.StandardError.ReadToEnd()
            $expected = (Join-Path $folder $case.Name) + [Environment]::NewLine
            if ($out -cne $expected -or $err -or $proc.ExitCode -ne 0) {
                throw "Unexpected result status=$($proc.ExitCode) stdout='$out' stderr='$err'"
            }
            Write-Host "$($case.Query) exit=0 stderrBytes=0 stdout=$($out.TrimEnd())"
        } finally {
            if ($gui -and !$gui.HasExited) {
                [void][PickerDriver]::PostMessage($gui.MainWindowHandle,16,0,0)
                [void]$gui.WaitForExit(5000)
            }
            $proc.Dispose()
        }
    }
} finally {
    $locked.Dispose()
    if ($previousDpi -ne [IntPtr]::Zero) { [void][PickerDriver]::SetThreadDpiAwarenessContext($previousDpi) }
    $owned = (Get-Item -LiteralPath $isolated).FullName
    if ([IO.Path]::GetDirectoryName($owned) -eq $Root) {
        Remove-Item -LiteralPath $owned -Recurse -Force
    }
}
