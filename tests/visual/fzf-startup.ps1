param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Fzf, [Parameter(Mandatory)][string]$Root, [ValidateRange(0,5000)][int]$DelayMs = 0)
$ErrorActionPreference = 'Stop'
$env:TERMOLE_PROBE_ENTRY = (Get-Item -LiteralPath (Join-Path $Package 'share/termole/examples/select-file.ps1')).FullName
$fzfItem = Get-Item -LiteralPath $Fzf
$env:Path = $fzfItem.DirectoryName + ';' + $env:Path
$run = [IO.Directory]::CreateDirectory((Join-Path $Root ([guid]::NewGuid().ToString('N')))).FullName
$env:TERMOLE_PROBE_TITLE = 'probe-' + [IO.Path]::GetFileName($run)
$env:TERMOLE_PROBE_DIR = [IO.Directory]::CreateDirectory("$run/files").FullName
foreach ($leaf in 'alpha.txt','beta.txt') {
    [IO.File]::WriteAllText((Join-Path $env:TERMOLE_PROBE_DIR $leaf), $leaf)
}
if ((@(Get-ChildItem -LiteralPath $env:TERMOLE_PROBE_DIR -File).Name -join ',') -cne 'alpha.txt,beta.txt') {
    throw 'Unexpected input order'
}
$env:APPDATA = "$run/profile"
$config = [IO.Directory]::CreateDirectory("$env:APPDATA/Termole").FullName
@('columns = 80','rows = 24', ('title = "' + $env:TERMOLE_PROBE_TITLE + '"')) | Set-Content -Encoding Ascii "$config/config.toml"
$env:FZF_DEFAULT_OPTS = $null
$env:FZF_DEFAULT_OPTS_FILE = $null
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ProbeView {
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr key, IntPtr flags);
}
"@
$command = @'
$ProgressPreference = 'SilentlyContinue'
$start = [Diagnostics.Stopwatch]::GetTimestamp()
$selection = & $env:TERMOLE_PROBE_ENTRY -Directory $env:TERMOLE_PROBE_DIR
$end = [Diagnostics.Stopwatch]::GetTimestamp()
if ($LASTEXITCODE -ne 0 -or @($selection).Count -ne 1 -or $selection -cne (Join-Path $env:TERMOLE_PROBE_DIR 'beta.txt')) {
    [Console]::Error.WriteLine("exit=$LASTEXITCODE selection='$selection'")
    exit 1
}
@{start=$start; end=$end; selection=$selection} | ConvertTo-Json -Compress
'@
$settings = New-Object Diagnostics.ProcessStartInfo powershell.exe
$settings.Arguments = '-NoProfile -ExecutionPolicy Bypass -NonInteractive -EncodedCommand ' + [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
$settings.UseShellExecute = $false
$settings.CreateNoWindow = $true
$settings.RedirectStandardOutput = $true
$settings.RedirectStandardError = $true
$proc = [Diagnostics.Process]::Start($settings)
$timer = [Diagnostics.Stopwatch]::StartNew()
$posts = 0
$window = [IntPtr]::Zero
try {
    while (!$proc.HasExited -and $timer.Elapsed.TotalSeconds -lt 15) {
        $window = [ProbeView]::FindWindow('TermoleStandaloneWindow',$env:TERMOLE_PROBE_TITLE)
        if ($window -ne [IntPtr]::Zero) {
            $view = [ProbeView]::FindWindowEx($window,0,'TermoleViewWindow',$null)
            $beforePost = [Diagnostics.Stopwatch]::GetTimestamp()
            if ($view -ne [IntPtr]::Zero -and !$posts) {
                if ($DelayMs) { Start-Sleep -Milliseconds $DelayMs }
                $firstPost = [Diagnostics.Stopwatch]::GetTimestamp()
                if (![ProbeView]::PostMessage($view,256,38,1)) { throw "Cannot post Up" }
                $afterPost = [Diagnostics.Stopwatch]::GetTimestamp()
                $posts++
            } elseif ($view -ne [IntPtr]::Zero -and ($beforePost - $firstPost) -gt [Diagnostics.Stopwatch]::Frequency / 4) {
                [void][ProbeView]::PostMessage($view,256,13,1)
            }
        }
        Start-Sleep -Milliseconds 10
    }
    if (!$proc.HasExited) { throw "Timed out waiting for fzf selection (window=$window, up posts=$posts)" }
    $out = $proc.StandardOutput.ReadToEnd()
    $err = $proc.StandardError.ReadToEnd()
    if ($proc.ExitCode -ne 0 -or $err -or !$posts) { throw "exit=$($proc.ExitCode) stderr=$err posts=$posts" }
    $result = $out | ConvertFrom-Json
    $ticksPerMs = [Diagnostics.Stopwatch]::Frequency / 1000
    [pscustomobject]@{
        first_post_ms = [math]::Round(($firstPost - $result.start) / $ticksPerMs, 3)
        queued_ms = [math]::Round(($afterPost - $result.start) / $ticksPerMs, 3)
        confirmed_ms = [math]::Round(($result.end - $result.start) / $ticksPerMs, 3)
        up_posts = $posts
        selection = $result.selection
    }
} finally {
    if (!$proc.HasExited) {
        if ($window -ne [IntPtr]::Zero) { [void][ProbeView]::PostMessage($window,16,0,0) }
        if (!$proc.WaitForExit(3000)) { $proc.Kill() }
    }
    $proc.Dispose()
}
