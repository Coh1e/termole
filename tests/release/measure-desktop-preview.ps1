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
$pointerOverlaps = 0
$pointerUnknown = 0
$failures = [Collections.Generic.List[string]]::new()
$gaps = [Collections.Generic.List[string]]::new()
$firstShort = $null
$firstClean = $null
$firstShortPredecessor = $null
$previousQpc = [long]0
$lastIndex = -1
foreach ($record in Import-Csv -LiteralPath "$Frames/dxgi.csv") {
    ++$lastIndex
    if ([int]$record.index -ne $lastIndex) { throw 'Missing DXGI acquisition record.' }
    $protected = [int]$record.protectedContentMaskedOut
    $masked += $protected
    # Mouse-only acquisitions can change visibility without changing the texture.
    $pointer = $record.pointerState
    $legacy = $null -eq $record.PSObject.Properties['pointerState']
    if ($pointer -eq 'overlap') { ++$pointerOverlaps }
    elseif ($pointer -ne 'clear') { ++$pointerUnknown }
    $qpc = [long]$record.lastPresentQpc
    if ($qpc -eq 0) { continue } # A mouse-only update has no desktop presentation.
    if ($qpc -le $previousQpc -or [int]$record.accumulatedFrames -lt 1) {
        throw 'Invalid DXGI presentation order or frame count.'
    }
    $previousQpc = $qpc
    $missed += [int]$record.accumulatedFrames - 1
    $image = [int]$record.imageIndex
    # Archived ledgers still supply positive pixel evidence. Without pointer
    # metadata they cannot certify a clean run. Never judge a known obscured frame.
    if ($protected -or $image -lt 0 -or (!$legacy -and $pointer -ne 'clear')) {
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
        Accumulated = [int]$record.accumulatedFrames; Masked = $protected; PointerState = $pointer
        DifferentPixels = $metric.DifferentPixels; TailInk = $metric.TailInk
        FromGlossary = $metric.FromGlossary; FromLicense = $metric.FromLicense
    })
    if ($metric.State -eq 'unavailable') { $gaps.Add("Unobservable presentation at $time ms"); continue }
    switch ($metric.State) {
        'black'   { $gaps.Add("Black preview at $time ms") }
        'unknown' { $gaps.Add("Unclassified preview at $time ms") }
        'mixed'   { $failures.Add("Short heading with $($metric.TailInk) extra tail pixels at $time ms") }
        'short-with-differences' { $failures.Add("Short heading with a different preview at $time ms") }
    }
    if ($metric.State -in 'short','mixed','short-with-differences') {
        if ($null -eq $firstShort) {
            $firstShort = $time
            $previous = if ($presentations.Count -gt 1 -and $presentations[-1].Accumulated -eq 1) {
                $presentations[-2]
            }
            $firstShortPredecessor = if ($previous) { $previous.State } else { 'unavailable' }
            if ($firstShortPredecessor -ne 'LICENSE' -or $previous.PresentMs -lt 0) {
                $gaps.Add("First short preview must immediately follow a post-input LICENSE presentation (previous: $firstShortPredecessor).")
            }
        }
        if ($null -eq $firstClean -and $metric.State -eq 'short') { $firstClean = $time }
    }
    if ($null -ne $firstShort -and $metric.State -in 'GLOSSARY','LICENSE') {
        $failures.Add("Previous preview reappeared at $time ms")
    }
}
if ($missed) { $gaps.Add("DXGI accumulated $missed additional frames") }
if ($masked) { $gaps.Add("DXGI reported $masked protected updates") }
if ($pointerOverlaps) { $gaps.Add("Pointer overlapped the preview on $pointerOverlaps acquisitions") }
if ($pointerUnknown) { $gaps.Add("Pointer visibility was unverified on $pointerUnknown acquisitions") }
if (!$presentations.Count -or $presentations[0].State -ne 'GLOSSARY' -or
    $presentations[0].PresentMs -gt 0) { $gaps.Add('Missing GLOSSARY baseline before input') }
if ($null -eq $firstShort -or $firstShort -lt 0 -or $presentations[-1].State -ne 'short') {
    $gaps.Add('Missing transition to the clean short preview')
}
$status = if ($failures.Count) { 'failed' } elseif ($gaps.Count) { 'inconclusive' } else { 'passed' }
[pscustomobject]@{
    Status = $status; FirstShortPredecessor = $firstShortPredecessor; FirstShortMs = $firstShort; FirstCleanMs = $firstClean
    Presentations = $presentations.Count; Missed = $missed; Masked = $masked
    PointerOverlaps = $pointerOverlaps; PointerUnknown = $pointerUnknown
    Failures = $failures.ToArray(); Gaps = $gaps.ToArray(); Frames = $presentations.ToArray()
}
