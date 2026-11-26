#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Evidence,
    [Parameter(Mandatory)][string]$Fzf
)

$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -ne 5) { throw 'Run with Windows PowerShell 5.1' }
$entry = (Get-Item -LiteralPath "$Package/share/termole/examples/select-file.ps1").FullName
$fzfFile = Get-Item -LiteralPath $Fzf
if ($fzfFile.Name -ine 'fzf.exe') { throw "Expected fzf.exe at $Fzf" }
$Evidence = [IO.Directory]::CreateDirectory($Evidence).FullName
if (@(Get-ChildItem -LiteralPath $Evidence -Force).Count) { throw "Use an empty evidence directory: $Evidence" }
$capture = (Get-Item -LiteralPath "$PSScriptRoot/../../scripts/capture-window.ps1").FullName
. "$PSScriptRoot/applet-desktop.ps1"
Add-Type -Path "$PSScriptRoot/applet-window.cs" -ReferencedAssemblies System.Drawing.dll
$running = [Collections.Generic.List[Diagnostics.Process]]::new()
$saved = @{}
foreach ($name in 'PATH','APPDATA','TEMP','TMP','FZF_DEFAULT_OPTS','FZF_DEFAULT_OPTS_FILE') {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
}
$dpi = [AppletWindow]::SetThreadDpiAwarenessContext(-4)
$report = [ordered]@{ status='running'; package=$Package; windows=[Environment]::OSVersion.VersionString
    powershell=$PSVersionTable.PSVersion.ToString(); fzf=(& $fzfFile.FullName --version | Out-String).Trim() }
try {
    $env:PATH = $fzfFile.DirectoryName + ';' + $env:PATH
    $env:TEMP = [IO.Directory]::CreateDirectory("$Evidence/temp").FullName
    $env:TMP = $env:TEMP
    $env:APPDATA = [IO.Directory]::CreateDirectory("$Evidence/profile").FullName
    $env:FZF_DEFAULT_OPTS = $null
    $env:FZF_DEFAULT_OPTS_FILE = $null
    & "$PSScriptRoot/applet-example.ps1" -Package $Package -Root "$Evidence/example"
    & "$PSScriptRoot/applet-preview.ps1" -Package $Package -Root "$Evidence/preview"

    $folder = [IO.Directory]::CreateDirectory("$Evidence/files").FullName
    $control = [IO.Directory]::CreateDirectory("$Evidence/reference").FullName
    $target = 'target ' + [char]0x96ea + ' %PATH% & [1].txt'
    $cases = @(
        @{ Query='alpha'; Name='alpha.txt'; Preview="[Truncated at 16 KiB]`nALPHA PREVIEW" },
        @{ Query='binary'; Name='binary.bin'; Preview='[Binary file]' },
        @{ Query='locked'; Name='locked.txt'; Preview='[File unreadable]' },
        @{ Query='target'; Name=$target; Preview='CURRENT PREVIEW' }
    )
    foreach ($case in $cases) { [IO.File]::WriteAllText("$control/$($case.Name)", $case.Preview) }
    [IO.File]::WriteAllText("$folder/alpha.txt", "ALPHA PREVIEW`n" * 1600)
    [IO.File]::WriteAllBytes("$folder/binary.bin", [byte[]](0,65,27))
    [IO.File]::WriteAllText("$folder/locked.txt", 'never displayed')
    [IO.File]::WriteAllText("$folder/$target", 'CURRENT PREVIEW')
    $signatures = @{}
    $locked = [IO.File]::Open("$folder/locked.txt", 'Open', 'ReadWrite', 'None')
    try {
        foreach ($case in $cases) {
            foreach ($source in $control, $folder) {
                $label = $case.Query
                if ($source -eq $control) { $label += '-reference' }
                $picker = Start-Applet $source $label -DefaultDirectory:($case.Query -eq 'alpha')
                Start-Sleep -Seconds 2
                foreach ($key in 38,38,40,38,40) { Send-AppletMessage $picker.View 256 $key }
                foreach ($char in $case.Query.ToCharArray()) { Send-AppletMessage $picker.View 258 ([int]$char) }
                # Let fzf cancel old previews and paint the final result. The reference files
                # spell out the expected lines without triggering truncation, decoding or locks.
                Start-Sleep -Seconds 2
                $deadline = [Diagnostics.Stopwatch]::StartNew()
                do {
                    $png = Save-AppletView $picker $label
                    $signature = [AppletWindow]::PreviewSignature($png)
                    if ($source -eq $control -and $signature) { break }
                    if ($signature -and $signature -ceq $signatures[$case.Query]) { break }
                    Start-Sleep -Milliseconds 250
                } while ($deadline.Elapsed.TotalSeconds -lt 10)
                if (!$signature) { throw "No preview glyphs in $png" }
                if ($source -eq $control) {
                    if ($signatures.ContainsValue($signature)) { throw "Different reference previews look identical: $png" }
                    $signatures[$case.Query] = $signature
                } elseif ($signature -cne $signatures[$case.Query]) { throw "Preview differs from $label-reference.png: $png" }
                Send-AppletMessage $picker.View 256 13
                Test-AppletResult $picker 0 ((Join-Path $source $case.Name) + "`r`n")
            }
        }
    } finally { $locked.Dispose() }

    $firstDir = [IO.Directory]::CreateDirectory("$Evidence/first").FullName
    $secondDir = [IO.Directory]::CreateDirectory("$Evidence/second").FullName
    foreach ($dir in $firstDir, $secondDir) {
        foreach ($leaf in 'alpha.txt','beta.txt') { [IO.File]::WriteAllText("$dir/$leaf", $leaf) }
    }
    $first = Start-Applet $firstDir 'escape'
    $survivor = Start-Applet $secondDir 'survivor'
    Test-AppletCancel $first 'escape'
    foreach ($char in 'beta'.ToCharArray()) { Send-AppletMessage $survivor.View 258 ([int]$char) }
    Start-Sleep -Seconds 2
    $null = Save-AppletView $survivor survivor
    Send-AppletMessage $survivor.View 256 13
    Test-AppletResult $survivor 0 ((Join-Path $secondDir 'beta.txt') + "`r`n")
    Test-AppletCancel (Start-Applet $firstDir 'close') 'close'

    # One Up per new process, selected path checked by the calling shell. The OS file
    # cache stays warm. Record samples; there is no latency pass/fail threshold.
    $report['firstKey'] = @(foreach ($i in 1..5) { Measure-AppletFirstKey $firstDir $i })
    $report['medianFirstPostMs'] = @($report.firstKey.firstPostMs | Sort-Object)[2]
    $report.status = 'passed'
} catch {
    $report.status = 'failed'
    $report['failure'] = $_.ToString()
    throw
} finally {
    foreach ($process in $running) {
        if (!$process.HasExited) { & "$env:SystemRoot/System32/taskkill.exe" /PID $process.Id /T /F | Out-Null }
        $process.Dispose()
    }
    if ($dpi -ne [IntPtr]::Zero) { [void][AppletWindow]::SetThreadDpiAwarenessContext($dpi) }
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$Evidence/result.json" -Encoding UTF8
    Write-Host "Applet $($report.status): $Evidence"
}
