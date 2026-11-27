#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Evidence,
    [Parameter(Mandatory)][string]$Fzf
)

$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -ne 5) { throw 'Run with Windows PowerShell 5.1' }
$Package = (Get-Item -LiteralPath $Package).FullName
$entry = (Get-Item -LiteralPath "$Package/share/termole/examples/select-file.ps1").FullName
$fzfFile = Get-Item -LiteralPath $Fzf
if ($fzfFile.Name -ine 'fzf.exe') { throw "Expected fzf.exe at $Fzf" }
$Evidence = [IO.Directory]::CreateDirectory($Evidence).FullName
if (@(Get-ChildItem -LiteralPath $Evidence -Force).Count) { throw "Use an empty evidence directory: $Evidence" }
$capture = (Get-Item -LiteralPath "$PSScriptRoot/../../scripts/capture-window.ps1").FullName
. "$PSScriptRoot/applet-desktop.ps1"
Add-Type -Path "$PSScriptRoot/applet-window.cs" -ReferencedAssemblies System.Drawing.dll
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
        @{ Query='target'; Name=$target; Preview='CURRENT PREVIEW' },
        @{ Query='readme'; Name='README.md'; Preview='# Termole'; PackageRoot=$true }
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
            $sourceFolder = if ($case.PackageRoot) { $Package } else { $folder }
            foreach ($source in $control, $sourceFolder) {
                $label = $case.Query
                if ($source -eq $control) { $label += '-reference' }
                $picker = Start-Applet $source $label -DefaultDirectory:($case.Query -eq 'alpha') -PackageRoot:($source -eq $Package)
                Start-Sleep -Seconds 2
                foreach ($key in 38,38,40,38,40) { Send-AppletMessage $picker.View 256 $key }
                foreach ($char in $case.Query.ToCharArray()) { Send-AppletMessage $picker.View 258 ([int]$char) }
                # Let fzf cancel old previews and paint the final result. The reference files
                # spell out the expected lines without triggering truncation, decoding or locks.
                Start-Sleep -Seconds 2
                $signature = Wait-AppletPreview $picker $label $signatures[$case.Query]
                if ($source -eq $control) {
                    if ($signatures.ContainsValue($signature)) { throw "Different reference previews look identical: $label.png" }
                    $signatures[$case.Query] = $signature
                }
                Send-AppletMessage $picker.View 256 13
                Test-AppletResult $picker 0 ((Join-Path $source $case.Name) + "`r`n")
            }
        }
    } finally { $locked.Dispose() }

    $firstDir = [IO.Directory]::CreateDirectory("$Evidence/first").FullName
    $secondDir = [IO.Directory]::CreateDirectory("$Evidence/second").FullName
    foreach ($leaf in 'first-alpha.txt','first-beta.txt') { [IO.File]::WriteAllText("$firstDir/$leaf", $leaf) }
    # Each partial query has a shorter match. Only both committed characters select the target.
    $zhong = [char]0x4e2d
    $wen = [char]0x6587
    $survivorName = [string]$zhong + $wen + '-target.txt'
    [IO.File]::WriteAllText("$secondDir/0.txt", '[File unreadable]')
    [IO.File]::WriteAllText("$secondDir/$zhong.txt", '[Binary file]')
    [IO.File]::WriteAllText("$secondDir/$wen.txt", '[File unreadable]')
    [IO.File]::WriteAllText("$secondDir/$survivorName", 'CURRENT PREVIEW')
    $first = Start-Applet $firstDir 'escape'
    $survivor = Start-Applet $secondDir 'survivor'
    $null = Wait-AppletPreview $survivor 'survivor-initial' $signatures['locked']
    Send-AppletMessage $survivor.View 258 ([int]$zhong)
    $null = Wait-AppletPreview $survivor 'survivor-first-character' $signatures['binary']
    Test-AppletCancel $first 'escape'
    Send-AppletMessage $survivor.View 258 ([int]$wen)
    $null = Wait-AppletPreview $survivor 'survivor' $signatures['target']
    Send-AppletMessage $survivor.View 256 13
    Test-AppletResult $survivor 0 ((Join-Path $secondDir $survivorName) + "`r`n")
    Test-AppletCancel (Start-Applet $firstDir 'close') 'close'

    # One Up per new process, selected path checked by the calling shell. The OS file
    # cache stays warm. Record samples; there is no latency pass/fail threshold.
    $timingDir = [IO.Directory]::CreateDirectory("$Evidence/timing").FullName
    foreach ($leaf in 'alpha.txt','beta.txt') { [IO.File]::WriteAllText("$timingDir/$leaf", $leaf) }
    $report['firstKey'] = @(foreach ($i in 1..5) { Measure-AppletFirstKey $timingDir $i })
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
