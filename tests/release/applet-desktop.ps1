# Windows PowerShell 5.1 helpers for the candidate's file picker.
# Each picker uses an isolated profile and unique window title.
function Start-Applet($Folder, $Name, [switch]$DefaultDirectory, [switch]$Measure, [switch]$PackageRoot,
                      [string[]]$WindowConfig = @('columns = 80', 'rows = 24')) {
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
    if ($Measure) {
        # Start the clock in the calling shell immediately before the shipped example.
        $start.EnvironmentVariables['TERMOLE_TEST_PICKER'] = $entry
        $command = @'
$ProgressPreference = 'SilentlyContinue'
$start = [Diagnostics.Stopwatch]::GetTimestamp()
$chosen = & $env:TERMOLE_TEST_PICKER
$end = [Diagnostics.Stopwatch]::GetTimestamp()
if ($LASTEXITCODE -ne 0 -or @($chosen).Count -ne 1 -or $chosen -cne (Join-Path $PWD 'beta.txt')) {
    [Console]::Error.WriteLine("Up did not select beta.txt: exit=$LASTEXITCODE path=$chosen")
    exit 1
}
@{start=$start; end=$end} | ConvertTo-Json -Compress
'@
        $start.Arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ' +
            [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    } elseif ($PackageRoot) {
        $start.Arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ".\share\termole\examples\select-file.ps1" -Directory .'
    } else {
        $start.Arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $entry + '"'
        if (!$DefaultDirectory) { $start.Arguments += ' -Directory "' + $Folder + '"' }
    }
    $process = [Diagnostics.Process]::Start($start)
    $running.Add($process)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (!$process.HasExited -and $timer.Elapsed.TotalSeconds -lt 20) {
        $window = [AppletWindow]::FindWindow('TermoleStandaloneWindow', $title)
        if ($window -ne [IntPtr]::Zero) {
            $view = [AppletWindow]::FindWindowEx($window, 0, 'TermoleViewWindow', $null)
            if ($view -ne [IntPtr]::Zero) {
                return @{ Window=$window; View=$view; Process=$process; Name=$Name; Folder=$Folder }
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

function Test-AppletResult($Picker, $Status, $Expected) {
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
    if ($p.ExitCode -ne $Status) { throw "$($Picker.Name) returned $($p.ExitCode), expected $Status" }
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

function Test-AppletCancel($Picker, $Mode) {
    Start-Sleep -Seconds 2
    $null = Save-AppletView $Picker $Mode
    $fzfProcess = Find-RunningDescendant fzf $Picker.Process.Id
    if (!$fzfProcess) { throw "No fzf running before $Mode" }
    $preview = $null
    try {
        # A fresh preview must be running at cancellation, not just observed earlier.
        Send-AppletMessage $Picker.View 256 38
        $deadline = [Diagnostics.Stopwatch]::StartNew()
        while (!$preview -and $deadline.Elapsed.TotalSeconds -lt 5) {
            $preview = Find-RunningDescendant powershell $fzfProcess.Id
            if (!$preview) { Start-Sleep -Milliseconds 10 }
        }
        if (!$preview -or $preview.HasExited) { throw "No live preview to cancel via $Mode" }
        [long]$dispatch = 0
        [AppletWindow]::GetSystemTimePreciseAsFileTime([ref]$dispatch)
        if ($Mode -eq 'escape') { Send-AppletMessage $Picker.View 256 27 }
        else { Send-AppletMessage $Picker.Window 16 0 }
        Test-AppletResult $Picker 130 ''
        $callerExit = $Picker.Process.ExitTime.ToFileTimeUtc()
        foreach ($child in $preview, $fzfProcess) {
            if (!$child.HasExited -or $child.ExitTime.ToFileTimeUtc() -lt $dispatch -or
                $child.ExitTime.ToFileTimeUtc() -gt $callerExit) {
                throw "PID $($child.Id) did not exit between $Mode and caller exit"
            }
        }
        [ordered]@{ dispatchUtc=[DateTime]::FromFileTimeUtc($dispatch).ToString('o')
            previewId=$preview.Id; previewExitUtc=$preview.ExitTime.ToUniversalTime().ToString('o')
            fzfId=$fzfProcess.Id; fzfExitUtc=$fzfProcess.ExitTime.ToUniversalTime().ToString('o')
            callerId=$Picker.Process.Id; callerExitUtc=$Picker.Process.ExitTime.ToUniversalTime().ToString('o')
        } | ConvertTo-Json | Set-Content -LiteralPath "$Evidence/$Mode-timeline.json" -Encoding UTF8
    } finally {
        if ($preview) { $preview.Dispose() }
        $fzfProcess.Dispose()
    }
}

function Measure-AppletFirstKey($Folder, $Sample) {
    $picker = Start-Applet $Folder "timing-$Sample" -Measure
    $first = [Diagnostics.Stopwatch]::GetTimestamp()
    Send-AppletMessage $picker.View 256 38
    $queued = [Diagnostics.Stopwatch]::GetTimestamp()
    Start-Sleep -Milliseconds 250
    Send-AppletMessage $picker.View 256 13
    $p = $picker.Process
    if (!$p.WaitForExit(15000)) { throw "First-key sample $Sample timed out" }
    $err = $p.StandardError.ReadToEnd()
    if ($p.ExitCode -ne 0 -or $err) { throw "First-key sample $Sample returned $($p.ExitCode): $err" }
    $result = $p.StandardOutput.ReadToEnd() | ConvertFrom-Json
    $ticksPerMs = [Diagnostics.Stopwatch]::Frequency / 1000
    [pscustomobject]@{ sample=$Sample; firstPostMs=[math]::Round(($first - $result.start) / $ticksPerMs, 3)
        queuedMs=[math]::Round(($queued - $result.start) / $ticksPerMs, 3)
        confirmedMs=[math]::Round(($result.end - $result.start) / $ticksPerMs, 3) }
}
