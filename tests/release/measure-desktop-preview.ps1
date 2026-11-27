param(
    [Parameter(Mandatory)][string]$Frames,
    [Parameter(Mandatory)][string]$Short,
    [Parameter(Mandatory)][string]$Glossary,
    [Parameter(Mandatory)][string]$License,
    [Parameter(Mandatory)][string]$Glyphs,
    [ValidateSet('bmp','png')][string]$ImageExtension = 'bmp'
)
$ErrorActionPreference = 'Stop'
if (!('DesktopPreviewPixels' -as [type])) {
    Add-Type -Path "$PSScriptRoot/desktop-preview-pixels.cs" -ReferencedAssemblies System.Drawing.dll
}
$classifier = [DesktopPreviewPixels]::new($Short, $Glossary, $License, $Glyphs)
$pixels = @{}
$presentations = [Collections.Generic.List[object]]::new()
$missed = 0
$masked = 0
$failures = [Collections.Generic.List[string]]::new()
$gaps = [Collections.Generic.List[string]]::new()
$firstShort = $null
$firstClean = $null
$sawLicense = $false
$previousQpc = [long]0
$lastIndex = -1
foreach ($record in Import-Csv -LiteralPath "$Frames/dxgi.csv") {
    ++$lastIndex
    if ([int]$record.index -ne $lastIndex) { throw 'Missing DXGI acquisition record.' }
    $protected = [int]$record.protectedContentMaskedOut
    $masked += $protected
    $qpc = [long]$record.lastPresentQpc
    if ($qpc -eq 0) { continue } # A mouse-only update has no desktop presentation.
    if ($qpc -le $previousQpc -or [int]$record.accumulatedFrames -lt 1) {
        throw 'Invalid DXGI presentation order or frame count.'
    }
    $previousQpc = $qpc
    $missed += [int]$record.accumulatedFrames - 1
    $image = [int]$record.imageIndex
    if ($protected -or $image -lt 0) {
        $metric = [pscustomobject]@{ State='unavailable'; DifferentPixels=$null; TailInk=$null; FromGlossary=$null; FromLicense=$null }
    } else {
        if (!$pixels.ContainsKey($image)) {
            $name = 'surface-{0:d4}.{1}' -f $image, $ImageExtension
            $pixels[$image] = $classifier.Compare((Join-Path $Frames $name))
        }
        $metric = $pixels[$image]
    }
    $time = [double]::Parse($record.presentMs, [Globalization.CultureInfo]::InvariantCulture)
    $presentations.Add([pscustomobject]@{
        Index = $lastIndex; PresentMs = $time; Image = $image; State = $metric.State
        Accumulated = [int]$record.accumulatedFrames; Masked = $protected
        DifferentPixels = $metric.DifferentPixels; TailInk = $metric.TailInk
        FromGlossary = $metric.FromGlossary; FromLicense = $metric.FromLicense
    })
    if ($metric.State -eq 'unavailable') { $gaps.Add("Unobservable presentation at $time ms"); continue }
    switch ($metric.State) {
        'LICENSE' { if ($time -ge 0) { $sawLicense = $true } }
        'black'   { $gaps.Add("Black preview at $time ms") }
        'unknown' { $gaps.Add("Unclassified preview at $time ms") }
        'mixed'   { $failures.Add("Short heading with $($metric.TailInk) extra tail pixels at $time ms") }
        'short-with-differences' { $failures.Add("Short heading with a different preview at $time ms") }
    }
    if ($metric.State -in 'short','mixed','short-with-differences') {
        if ($null -eq $firstShort) { $firstShort = $time }
        if ($null -eq $firstClean -and $metric.State -eq 'short') { $firstClean = $time }
    }
    if ($null -ne $firstShort -and $metric.State -in 'GLOSSARY','LICENSE') {
        $failures.Add("Previous preview reappeared at $time ms")
    }
}
if ($missed) { $gaps.Add("DXGI accumulated $missed additional frames") }
if ($masked) { $gaps.Add("DXGI reported $masked protected updates") }
if (!$presentations.Count -or $presentations[0].State -ne 'GLOSSARY' -or
    $presentations[0].PresentMs -gt 0) { $gaps.Add('Missing GLOSSARY baseline before input') }
if ($null -eq $firstShort -or $firstShort -lt 0 -or $presentations[-1].State -ne 'short') {
    $gaps.Add('Missing transition to the clean short preview')
}
$status = if ($failures.Count) { 'failed' } elseif ($gaps.Count) { 'inconclusive' } else { 'passed' }
[pscustomobject]@{
    Status = $status; SawLicense = $sawLicense; FirstShortMs = $firstShort; FirstCleanMs = $firstClean
    Presentations = $presentations.Count; Missed = $missed; Masked = $masked
    Failures = $failures.ToArray(); Gaps = $gaps.ToArray(); Frames = $presentations.ToArray()
}
