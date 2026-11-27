# Replay the four losslessly PNG-encoded preview crops and the raw DXGI acquisition
# ledger from Coh1e/termole#54, issuecomment-7212010345 (20261127-145255-dxconsole-da3d1d7a).
param()
$ErrorActionPreference = 'Stop'
$frames = "$PSScriptRoot/fixtures/preview-residue"
$references = @{ ImageExtension='png'; Glossary="$frames/surface-0000.png";
    License="$frames/surface-0001.png"; Short="$frames/surface-0003.png" }
$measure = "$PSScriptRoot/measure-desktop-preview.ps1"
Add-Type -AssemblyName System.Drawing
function Write-HistoricalGlyphs($Path, $Padding = 0) {
    # Reconstruct the guarded line from the original independent 96-DPI long previews:
    # T from 'MIT', n from 'documentation', and the period from 'termole.exe' under GUI Role.
    $license = [Drawing.Bitmap]::new($references.License)
    $glossary = [Drawing.Bitmap]::new($references.Glossary)
    $control = [Drawing.Bitmap]::new($license.Width, $license.Height + $Padding)
    $draw = [Drawing.Graphics]::FromImage($control)
    try {
        $draw.Clear($license.GetPixel(0, 20))
        $draw.PageUnit = [Drawing.GraphicsUnit]::Pixel
        foreach ($cell in @(@($license,26,18,282), @($license,343,112,299),
                            @($glossary,120,546,308), @($license,26,18,326))) {
            $from = [Drawing.Rectangle]::new($cell[1], $cell[2], 9, 18)
            $to = [Drawing.Rectangle]::new($cell[3], 18, 9, 18)
            $draw.DrawImage($cell[0], $to, $from, [Drawing.GraphicsUnit]::Pixel)
        }
        $control.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    } finally { $draw.Dispose(); $control.Dispose(); $glossary.Dispose(); $license.Dispose() }
}
# Replay the same transition with a clean first frame, a merged present, a protected
# present without a bitmap, and a black/unreadable capture. Only the first is conclusive.
$scratch = [IO.Directory]::CreateDirectory((Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString()))).FullName
try {
    $references.Glyphs = "$scratch/suffix-glyphs.png"
    Write-HistoricalGlyphs $references.Glyphs
    $report = & $measure -Frames $frames @references
    $mixed = @($report.Frames | Where-Object State -eq 'mixed')
    if ($report.Status -ne 'failed' -or $report.Presentations -ne 7 -or $report.Missed -or $report.Masked -or
        $mixed.Count -ne 1 -or $mixed[0].Index -ne 6 -or $mixed[0].TailInk -ne 1379 -or
        $mixed[0].FromLicense -ne 1379 -or !$report.SawLicense -or
        $report.FirstShortMs -ne 602.5598 -or $report.FirstCleanMs -ne 633.5750) {
        throw ("Historical desktop residue was not detected: " + ($report | ConvertTo-Json -Depth 5 -Compress))
    }
    Get-ChildItem -LiteralPath $frames -Filter '*.png' | Copy-Item -Destination $scratch
    $rows = @(Import-Csv -LiteralPath "$frames/dxgi.csv")
    function Check-Replay($Expected, $FirstShortState) {
        $rows | Export-Csv -LiteralPath "$scratch/dxgi.csv" -NoTypeInformation
        $replay = & $measure -Frames $scratch @references
        if ($replay.Status -ne $Expected) { throw "Expected $Expected replay: $($replay | ConvertTo-Json -Depth 4)" }
        if ($FirstShortState) {
            $frame = $replay.Frames | Where-Object Index -eq 6
            if ($frame.State -ne $FirstShortState -or $replay.FirstShortMs -ne 602.5598) {
                throw "Expected $FirstShortState on the first short presentation, got $($frame.State)"
            }
        }
    }
    $rows[6].imageIndex = 3
    Check-Replay 'passed'
    $rows[6].accumulatedFrames = 2
    Check-Replay 'inconclusive'
    $rows[6].accumulatedFrames = 1
    $rows[6].protectedContentMaskedOut = 1
    $rows[6].imageIndex = -1
    Check-Replay 'inconclusive'
    $rows[6].protectedContentMaskedOut = 0
    Check-Replay 'inconclusive'

    $rows[6].imageIndex = 4
    $blank = [Drawing.Bitmap]::new(474, 594)
    $draw = [Drawing.Graphics]::FromImage($blank)
    try {
        foreach ($color in [Drawing.Color]::Black, [Drawing.Color]::Gray) {
            $draw.Clear($color)
            $blank.Save("$scratch/surface-0004.png", [Drawing.Imaging.ImageFormat]::Png)
            Check-Replay 'inconclusive'
        }
    } finally { $draw.Dispose(); $blank.Dispose() }
    # Even a partial new heading over the old LICENSE is a visible defect. The
    # first 80 pixels contain the first word and the beginning of the second one.
    $partial = [Drawing.Bitmap]::new($references.License)
    $shortImage = [Drawing.Bitmap]::new($references.Short)
    $draw = [Drawing.Graphics]::FromImage($partial)
    try {
        $prefix = [Drawing.Rectangle]::new(0, 0, 80, 37)
        $draw.DrawImage($shortImage, $prefix, $prefix, [Drawing.GraphicsUnit]::Pixel)
        $partial.Save("$scratch/surface-0004.png", [Drawing.Imaging.ImageFormat]::Png)
        Check-Replay 'failed' 'mixed'
    } finally { $draw.Dispose(); $shortImage.Dispose(); $partial.Dispose() }

    # Erase only the six pixels of the historical full stop. The tail is clean
    # and the next frame is perfect, but the first visible sentence is incomplete.
    $clipped = [Drawing.Bitmap]::new($references.Short)
    try {
        $background = $clipped.GetPixel(310, 29)
        foreach ($x in 311..313) {
            foreach ($y in 29..30) { $clipped.SetPixel($x, $y, $background) }
        }
        $clipped.Save("$scratch/surface-0004.png", [Drawing.Imaging.ImageFormat]::Png)
        Check-Replay 'failed' 'short-with-differences'
    } finally { $clipped.Dispose() }

    # Damage the short control AND both short presentations. The intact independent
    # glyphs must still reject a partly clipped n and a half-height period separately.
    $rows[7].imageIndex = 4
    $rows | Export-Csv -LiteralPath "$scratch/dxgi.csv" -NoTypeInformation
    $shared = $references.Clone()
    $shared.Short = "$scratch/surface-0004.png"
    foreach ($damage in [Drawing.Rectangle]::new(300,28,7,3), [Drawing.Rectangle]::new(311,29,3,1)) {
        $clipped = [Drawing.Bitmap]::new($references.Short)
        try {
            $background = $clipped.GetPixel(310, 29)
            for ($y = $damage.Top; $y -lt $damage.Bottom; $y++) {
                for ($x = $damage.Left; $x -lt $damage.Right; $x++) { $clipped.SetPixel($x, $y, $background) }
            }
            $clipped.Save($shared.Short, [Drawing.Imaging.ImageFormat]::Png)
        } finally { $clipped.Dispose() }
        try { $unexpected = & $measure -Frames $scratch @shared }
        catch {
            if ($_.Exception.GetBaseException().Message -ne "Short reference differs from the independent 'n.' glyphs.") { throw }
            continue
        }
        throw "Expected shared suffix damage at $damage to be rejected, got $($unexpected.Status)."
    }

    # A spare terminal row below the bottom border changes the crop height, not
    # the preview body. The actual 96 DPI recordings still contain 1,379 old pixels.
    foreach ($index in 0..3) {
        $name = 'surface-{0:d4}.png' -f $index
        $image = [Drawing.Bitmap]::new("$frames/$name")
        $padded = [Drawing.Bitmap]::new($image.Width, $image.Height + 20)
        $draw = [Drawing.Graphics]::FromImage($padded)
        try {
            $draw.Clear($image.GetPixel(0, $image.Height - 1))
            $draw.PageUnit = [Drawing.GraphicsUnit]::Pixel
            $rect = [Drawing.Rectangle]::new(0, 0, $image.Width, $image.Height)
            $draw.DrawImage($image, $rect, $rect, [Drawing.GraphicsUnit]::Pixel)
            $padded.Save("$scratch/$name", [Drawing.Imaging.ImageFormat]::Png)
        } finally { $draw.Dispose(); $padded.Dispose(); $image.Dispose() }
    }
    Write-HistoricalGlyphs "$scratch/padded-glyphs.png" 20
    $paddedPixels = [DesktopPreviewPixels]::new("$scratch/surface-0003.png", "$scratch/surface-0000.png",
        "$scratch/surface-0001.png", "$scratch/padded-glyphs.png")
    $paddedMixed = $paddedPixels.Compare("$scratch/surface-0002.png")
    if ($paddedMixed.State -ne 'mixed' -or $paddedMixed.FromLicense -ne 1379) {
        throw 'Moving the bottom border within the crop changed the historical result.'
    }

    # Compare the complete View and a desktop crop using physical offsets. A
    # single changed pixel at the bottom-right corner must invalidate a reference.
    $view = [Drawing.Bitmap]::new($references.Short)
    $region = [Drawing.Rectangle]::new(4, 7, 360, 415)
    $crop = $view.Clone($region, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bitmapPath = "$scratch/reference.bmp"
    try {
        foreach ($expectedDifference in 0,1) {
            if ($expectedDifference) { $crop.SetPixel(359, 414, [Drawing.Color]::Magenta) }
            $crop.Save($bitmapPath, [Drawing.Imaging.ImageFormat]::Bmp)
            $difference = [DesktopPreviewPixels]::ReferenceDifferences($references.Short, $bitmapPath,
                $region.Left, $region.Top, $view.Width, $view.Height, $region.Width, $region.Height)
            if ($difference -ne $expectedDifference) { throw "Unexpected desktop reference difference: $difference" }
        }
    } finally { $crop.Dispose(); $view.Dispose() }
} finally {
    Get-ChildItem -LiteralPath $scratch -File | Remove-Item
    [IO.Directory]::Delete($scratch)
}
[pscustomobject]@{
    Status = 'passed'; ResiduePixels = $mixed[0].FromLicense; FirstShortMs = $report.FirstShortMs
    FirstCleanMs = $report.FirstCleanMs; DurationMs = [math]::Round($report.FirstCleanMs - $report.FirstShortMs, 4)
}
