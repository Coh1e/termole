#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Evidence,
    [Parameter(Mandatory)][string]$Fzf,
    [Parameter(Mandatory)][Alias('Capture')][string]$DesktopCapture
)

$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -ne 5) { throw 'Run with Windows PowerShell 5.1' }
$Package = (Get-Item -LiteralPath $Package).FullName
$Evidence = [IO.Directory]::CreateDirectory($Evidence).FullName
if (@(Get-ChildItem -LiteralPath $Evidence -Force).Count) { throw "Use an empty evidence directory: $Evidence" }
$entry = (Get-Item -LiteralPath "$Package/share/termole/examples/select-file.ps1").FullName
$app = (Get-Item -LiteralPath "$Package/bin/termole.exe").FullName
$fzfFile = Get-Item -LiteralPath $Fzf
if ($fzfFile.Name -ine 'fzf.exe') { throw "Expected fzf.exe at $Fzf" }
$collector = (Get-Item -LiteralPath $DesktopCapture).FullName
$capture = (Get-Item -LiteralPath "$PSScriptRoot/../../scripts/capture-window.ps1").FullName
. "$PSScriptRoot/applet-desktop.ps1"
Add-Type -Path "$PSScriptRoot/applet-window.cs" -ReferencedAssemblies System.Drawing.dll

$short = 'lighthouse-6a98.txt'
$query = 'lighthouse-6a98'
$running = [Collections.Generic.List[Diagnostics.Process]]::new()
$windowConfig = @('columns = 111', 'rows = 32', 'font = "Consolas"', 'font_size = 16',
                  'theme = "light"', 'center = true')

function Read-DesktopPreview($Name, $Folder, $Selected, $ExpectedSignature, [switch]$SwitchFile) {
    $dir = [IO.Directory]::CreateDirectory("$Evidence/$Name").FullName
    $frames = [IO.Directory]::CreateDirectory("$dir/frames").FullName
    $scene = [ordered]@{ directory=$dir; frames=$frames; expectedPath=(Join-Path $Folder $Selected); outputVerified=$false }
    try {
        $picker = Start-Applet $Folder $Name -WindowConfig $windowConfig
        $scene.dpi = [AppletWindow]::GetDpiForWindow($picker.View)
        if (!$scene.dpi) { throw "Could not determine View DPI for $Name" }
        # Wait for the expected initial content before requesting desktop pixels.
        try {
            $scene.signature = Wait-AppletPreview $picker "$Name-ready" $ExpectedSignature
        } catch {
            $scene.captureExit = 77
            $scene.reason = "Initial window preview unavailable: $($_.Exception.Message)"
            return [pscustomobject]$scene
        }
        [uint32]$guiId = 0
        if (![AppletWindow]::GetWindowThreadProcessId($picker.Window, [ref]$guiId)) {
            throw "Could not identify the candidate's GUI process for $Name"
        }
        $gui = [Diagnostics.Process]::GetProcessById($guiId)
        try {
            if ($gui.MainModule.FileName -ine $app) { throw "Unexpected GUI executable for $Name" }
        } finally { $gui.Dispose() }
        $fzfProcess = Find-RunningDescendant fzf $guiId
        if (!$fzfProcess) { throw "No fzf descendant of $Name" }
        try {
            if ($fzfProcess.MainModule.FileName -ine $fzfFile.FullName) { throw "Unexpected fzf executable for $Name" }
            $scene.guiPid = $guiId
            $scene.fzfPid = $fzfProcess.Id
            $arguments = @($picker.Window.ToInt64(), $picker.View.ToInt64(), $app, $frames)
            if ($SwitchFile) { $arguments += @($fzfProcess.Id, $query) }
            $quoted = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
            $start = [Diagnostics.ProcessStartInfo]::new($collector, $quoted)
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $true
            $start.RedirectStandardOutput = $true
            $start.RedirectStandardError = $true
            $scene.foregroundBeforeCapture = [AppletWindow]::RaiseForDesktopCapture($picker.Window)
            # A newly activated picker can momentarily show an IME window above its preview.
            if (!$SwitchFile) { Start-Sleep -Milliseconds 300 }
            $native = [Diagnostics.Process]::Start($start)
            try {
                $out = $native.StandardOutput.ReadToEndAsync()
                $err = $native.StandardError.ReadToEndAsync()
                if (!$native.WaitForExit(15000)) {
                    $native.Kill()
                    throw "DXGI capture timed out for $Name"
                }
                [IO.File]::WriteAllText("$dir/capture.log", $out.Result)
                [IO.File]::WriteAllText("$dir/capture-error.log", $err.Result)
                $scene.captureExit = $native.ExitCode
                if ($scene.captureExit -notin 0,77) {
                    throw "DXGI capture failed with exit $($scene.captureExit). See $dir/capture-error.log"
                }
            } finally { $native.Dispose() }
        } finally { $fzfProcess.Dispose() }
        if ($scene.captureExit -eq 0) {
            if (!$SwitchFile) {
                $scene.referenceVerified = $false
                try {
                    # Capture after DXGI: the initial HWND may still have been resizing at startup.
                    $scene.windowAfter = Save-AppletView $picker "$Name-window-after"
                    $scene.dpiAfter = [AppletWindow]::GetDpiForWindow($picker.View)
                    $roi = @(Import-Csv -LiteralPath "$frames/roi.csv")
                    if ($roi.Count -ne 1) { throw 'Expected one desktop preview rectangle.' }
                    $r = $roi[0]
                    $scene.referenceDifferentPixels = [DesktopPreviewPixels]::ReferenceDifferences(
                        $scene.windowAfter, "$frames/surface-0000.bmp", $r.left, $r.top,
                        $r.viewWidth, $r.viewHeight, $r.width, $r.height)
                    $scene.signature = [AppletWindow]::PreviewSignature($scene.windowAfter)
                    $scene.referenceVerified = $scene.dpiAfter -eq $scene.dpi -and
                        $scene.referenceDifferentPixels -eq 0 -and [bool]$scene.signature
                    if (!$scene.referenceVerified) {
                        $scene.reason = "Stable HWND / DXGI reference differs: $($scene.referenceDifferentPixels) RGB pixels, DPI $($scene.dpi) to $($scene.dpiAfter)."
                    }
                } catch {
                    $scene.reason = "Independent reference could not be compared: $($_.Exception.Message)"
                }
            }
            Send-AppletMessage $picker.View 256 13
            Test-AppletResult $picker 0 ($scene.expectedPath + "`r`n")
            $scene.outputVerified = $true
        }
        return [pscustomobject]$scene
    } finally {
        foreach ($p in $running) {
            if (!$p.HasExited) { & "$env:SystemRoot/System32/taskkill.exe" /PID $p.Id /T /F | Out-Null }
            $p.Dispose()
        }
        $running.Clear()
        $scene | ConvertTo-Json | Set-Content -LiteralPath "$dir/scene.json" -Encoding UTF8
    }
}

$previousPath = $env:PATH
$dpiContext = [AppletWindow]::SetThreadDpiAwarenessContext(-4)
$report = [ordered]@{ status='running'; package=$Package; fzf=$fzfFile.FullName
    fzfVersion=(& $fzfFile.FullName --version | Out-String).Trim(); query=$query; referenceAttempts=@(); attempts=@() }
try {
    $report.control = & "$PSScriptRoot/test-desktop-preview.ps1"
    $report.control | ConvertTo-Json | Set-Content -LiteralPath "$Evidence/historical-control.json" -Encoding UTF8
    Write-Host "Historical mixed frame: $($report.control.ResiduePixels) old pixels at $($report.control.FirstShortMs) ms"
    $env:PATH = $fzfFile.DirectoryName + ';' + $previousPath
    $files = [IO.Directory]::CreateDirectory("$Evidence/files").FullName
    foreach ($name in 'GLOSSARY.md','LICENSE','README.md','THIRD_PARTY_NOTICES.md') {
        Copy-Item -LiteralPath (Join-Path $Package $name) -Destination (Join-Path $files $name)
    }
    [IO.File]::WriteAllText((Join-Path $files $short), 'The lighthouse turns amber at noon.',
                            [Text.UTF8Encoding]::new($false))

    $refs = @{}
    foreach ($name in 'GLOSSARY.md','LICENSE',$short) {
        $single = [IO.Directory]::CreateDirectory("$Evidence/single-$name").FullName
        Copy-Item -LiteralPath (Join-Path $files $name) -Destination (Join-Path $single $name)
        for ($i = 1; $i -le 3; $i++) {
            $ref = Read-DesktopPreview "ref-$name-$i" $single $name
            $report.referenceAttempts += [ordered]@{ name=$name; attempt=$i; captureExit=$ref.captureExit
                verified=($ref.outputVerified -and $ref.referenceVerified)
                differentPixels=$ref.referenceDifferentPixels; reason=$ref.reason }
            if ($ref.outputVerified -and $ref.referenceVerified) { break }
        }
        if (!$ref.outputVerified -or !$ref.referenceVerified) {
            $report.status = 'inconclusive'
            throw "No usable independent desktop reference for $name. $($ref.reason) See $($ref.directory)"
        }
        $refs[$name] = $ref
    }
    $displayDpi = $refs[$short].dpi
    if ($refs['LICENSE'].dpi -ne $displayDpi -or $refs['GLOSSARY.md'].dpi -ne $displayDpi) {
        $report.status = 'inconclusive'
        throw 'The independent references used different DPI settings.'
    }
    $report.dpi = $displayDpi
    $references = @{
        Short = (Join-Path $refs[$short].frames 'surface-0000.bmp')
        License = (Join-Path $refs['LICENSE'].frames 'surface-0000.bmp')
        Glossary = (Join-Path $refs['GLOSSARY.md'].frames 'surface-0000.bmp')
    }
    for ($i = 1; $i -le 6; $i++) {
        $scene = Read-DesktopPreview "switch-$i" $files $short $refs['GLOSSARY.md'].signature -SwitchFile
        $record = [ordered]@{ attempt=$i; captureExit=$scene.captureExit; outputVerified=$scene.outputVerified }
        $report.attempts += $record
        if ($scene.dpi -ne $displayDpi) { $record.reason = 'DPI changed'; continue }
        if (!(Test-Path -LiteralPath "$($scene.frames)/dxgi.csv")) {
            $record.reason = if ($scene.reason) { $scene.reason } else { 'No desktop presents were captured' }
            continue
        }
        $pixels = & "$PSScriptRoot/measure-desktop-preview.ps1" -Frames $scene.frames @references
        $pixels | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$($scene.directory)/pixels.json" -Encoding UTF8
        $record.status = $pixels.Status
        $record.sawLicense = $pixels.SawLicense
        $record.firstShortMs = $pixels.FirstShortMs
        $record.presentations = $pixels.Presentations
        if ($pixels.Status -eq 'failed') { throw "Desktop preview regression. See $($scene.directory)/pixels.json" }
        if ($pixels.Status -eq 'passed' -and $pixels.SawLicense -and $scene.outputVerified) {
            $report.status = 'passed'
            break
        }
    }
    if ($report.status -ne 'passed') {
        $report.status = 'inconclusive'
        throw "No complete GLOSSARY to LICENSE to short desktop transition in $($report.attempts.Count) attempts."
    }
} catch {
    if ($report.status -eq 'running') { $report.status = 'failed' }
    $report.error = $_.Exception.Message
    throw
} finally {
    $env:PATH = $previousPath
    if ($dpiContext -ne [IntPtr]::Zero) { [void][AppletWindow]::SetThreadDpiAwarenessContext($dpiContext) }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$Evidence/result.json" -Encoding UTF8
    Write-Host "DXGI desktop preview $($report.status): $Evidence"
}
