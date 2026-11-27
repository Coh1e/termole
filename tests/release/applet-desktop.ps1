# Windows PowerShell 5.1 helpers for the candidate's file picker.
# Each picker uses an isolated profile and unique window title.
function Start-Applet($Folder, $Name, [string[]]$WindowConfig) {
    $title = 'termole-test-' + [guid]::NewGuid().ToString('N')
    $profile = [IO.Directory]::CreateDirectory("$Evidence/profiles/$title/Termole").FullName
    [IO.File]::WriteAllLines("$profile/config.toml", ($WindowConfig + ('title = "' + $title + '"')))
    $start = [Diagnostics.ProcessStartInfo]::new("$PSHOME/powershell.exe")
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = $Folder
    $start.EnvironmentVariables['APPDATA'] = [IO.Path]::GetDirectoryName($profile)
    $start.Arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $entry + '" -Directory "' + $Folder + '"'
    $process = [Diagnostics.Process]::Start($start)
    $running.Add($process)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (!$process.HasExited -and $timer.Elapsed.TotalSeconds -lt 20) {
        $window = [AppletWindow]::FindWindow('TermoleStandaloneWindow', $title)
        if ($window -ne [IntPtr]::Zero) {
            $view = [AppletWindow]::FindWindowEx($window, 0, 'TermoleViewWindow', $null)
            if ($view -ne [IntPtr]::Zero) {
                return @{ Window=$window; View=$view; Process=$process; Name=$Name }
            }
        }
        Start-Sleep -Milliseconds 10
    }
    if ($process.HasExited) { throw "$Name exited $($process.ExitCode): $($process.StandardError.ReadToEnd())" }
    throw "No View HWND for $Name within 20 seconds"
}

function Send-AppletMessage($Window, $Message, $Key) {
    if (![AppletWindow]::PostMessage($Window, $Message, $Key, 1)) {
        throw "Posting $Message/$Key failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
}

function Save-AppletView($Picker, $Name) {
    $png = Join-Path $Evidence "$Name.png"
    & $capture -Hwnd $Picker.View.ToInt64() -OutputPath $png -StatsPath "$png.txt" -Quiet
    return $png
}

function Wait-AppletPreview($Picker, $Label, $Expected) {
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    do {
        $png = Save-AppletView $Picker $Label
        $signature = [AppletWindow]::PreviewSignature($png)
        if ($signature -and (!$Expected -or $signature -ceq $Expected)) { return $signature }
        Start-Sleep -Milliseconds 250
    } while ($deadline.Elapsed.TotalSeconds -lt 10)
    throw "Unexpected preview in ${png}: expected '$Expected', got '$signature'"
}

function Test-AppletResult($Picker, $Expected) {
    $p = $Picker.Process
    if (!$p.WaitForExit(15000)) { throw "$($Picker.Name) timed out" }
    foreach ($channel in 'StandardOutput','StandardError') {
        $file = Join-Path $Evidence "$($Picker.Name)-$channel.bin"
        $stream = [IO.File]::Create($file)
        try { $p.$channel.BaseStream.CopyTo($stream) } finally { $stream.Dispose() }
        $bytes = [IO.File]::ReadAllBytes($file)
        $want = if ($channel -eq 'StandardOutput') { [string]$Expected } else { '' }
        if ([Convert]::ToBase64String($bytes) -cne [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($want))) {
            throw "$($Picker.Name) returned unexpected bytes on $channel. See $file."
        }
    }
    if ($p.ExitCode -ne 0) { throw "$($Picker.Name) returned $($p.ExitCode), expected 0" }
}

function Find-RunningDescendant($Name, $AncestorId) {
    foreach ($p in @(Get-Process -Name $Name -ErrorAction SilentlyContinue)) {
        $keep = $false
        try {
            $parent = [AppletWindow]::ParentId($p)
            while ($parent -gt 0) {
                if ($parent -eq $AncestorId) {
                    $keep = !$p.HasExited
                    if ($keep) { return $p }
                    break
                }
                $owner = [Diagnostics.Process]::GetProcessById($parent)
                try { $parent = [AppletWindow]::ParentId($owner) } finally { $owner.Dispose() }
            }
        } catch [ArgumentException] { }
          catch [ComponentModel.Win32Exception] { }
          catch [InvalidOperationException] { }
        finally { if (!$keep) { $p.Dispose() } }
    }
}
