#requires -Version 5.1
<#
.SYNOPSIS
Capture an HWND with PrintWindow and report tolerant non-background pixel
statistics.

.DESCRIPTION
Writes a PNG of the window and prints (or writes next to the PNG) simple
statistics that a test can assert on:

  captured     1 when PrintWindow reported success
  width        captured bitmap width in pixels
  height       captured bitmap height in pixels
  colors          number of distinct RGB values
  background      most frequent RGB value (the window background)
  inkPixels       pixels whose strongest channel differs from the background
  inkRatio        inkPixels / (width * height)
  strongPixels    pixels whose strongest channel differs by >= 96
  bandStrongPixels strong pixels inside the optional row band

"ink" deliberately tolerates font antialiasing, theme colours and DPI: it
counts a pixel as text/foreground when its strongest channel differs from the
most frequent background by at least 24. "strong" uses 96 so a filled selection
highlight is distinguishable from glyph antialiasing; both are reported, not
asserted. The script never asserts what the pixels mean; callers decide the
threshold. A blank window has one colour and zero ink.

The window size is taken from GetWindowRect unless -Width/-Height are given.
Passing the size explicitly is more reliable from a DPI-unaware caller.

.EXAMPLE
powershell -NoProfile -File scripts/capture-window.ps1 `
  -Hwnd 0x000A1234 -OutputPath out\view.png -Width 1000 -Height 640

.EXAMPLE
powershell -NoProfile -File scripts/capture-window.ps1 `
  -Hwnd 12345678 -OutputPath out\view.png -StatsPath out\view.txt -Quiet
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Hwnd,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [int]$Width = 0,
    [int]$Height = 0,

    [string]$StatsPath,
    [int]$BandTop = -1,
    [int]$BandBottom = -1,
    [string]$TempPath,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# Add-Type compiles its helper in $env:TEMP. Sandboxed test processes can be
# barred from the user temp directory while still being able to write next to
# their artifacts, so keep the compiler scratch directory beside the PNG by
# default. Callers can override it with -TempPath.
if (-not $TempPath) {
    $absoluteOutput = [System.IO.Path]::GetFullPath($OutputPath)
    $outputDirectory = [System.IO.Path]::GetDirectoryName($absoluteOutput)
    $TempPath = [System.IO.Path]::Combine($outputDirectory, 'capture-temp')
}
New-Item -ItemType Directory -Force -Path $TempPath | Out-Null
$env:TEMP = $TempPath
$env:TMP = $TempPath

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class TermoleWindowCapture
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public struct Analysis
    {
        public int Colors;
        public int Background;
        public long Ink;
        public long BandInk;
        public long Strong;
        public long BandStrong;
    }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hwnd);

    // PW_RENDERFULLCONTENT: capture the composited content, not just the
    // window procedure's WM_PRINT response.
    public const uint PW_RENDERFULLCONTENT = 0x00000002;

    // Background = most frequent colour; ink = a pixel whose strongest channel
    // is at least 24 away from that background. This is intentionally coarse.
    // BandInk counts the same pixels only on rows bandTop..bandBottom; a
    // negative bandTop disables the band.
    public static Analysis Analyze(byte[] pixels, int width, int height, int stride,
                                   int bandTop, int bandBottom)
    {
        var counts = new Dictionary<int, int>();
        for (int y = 0; y < height; y++)
        {
            int row = y * stride;
            for (int x = 0; x < width; x++)
            {
                int index = row + x * 4;
                int color = (pixels[index + 2] << 16) | (pixels[index + 1] << 8) | pixels[index];
                int seen;
                counts.TryGetValue(color, out seen);
                counts[color] = seen + 1;
            }
        }

        int background = 0;
        int best = -1;
        foreach (KeyValuePair<int, int> entry in counts)
        {
            if (entry.Value > best)
            {
                best = entry.Value;
                background = entry.Key;
            }
        }

        int backgroundRed = (background >> 16) & 0xFF;
        int backgroundGreen = (background >> 8) & 0xFF;
        int backgroundBlue = background & 0xFF;

        long ink = 0;
        long bandInk = 0;
        long strong = 0;
        long bandStrong = 0;
        for (int y = 0; y < height; y++)
        {
            int row = y * stride;
            bool inBand = bandTop >= 0 && y >= bandTop && y <= bandBottom;
            for (int x = 0; x < width; x++)
            {
                int index = row + x * 4;
                int difference = Math.Max(
                    Math.Abs(pixels[index + 2] - backgroundRed),
                    Math.Max(Math.Abs(pixels[index + 1] - backgroundGreen),
                             Math.Abs(pixels[index] - backgroundBlue)));
                if (difference >= 24)
                {
                    ink++;
                    if (inBand) bandInk++;
                }
                if (difference >= 96)
                {
                    strong++;
                    if (inBand) bandStrong++;
                }
            }
        }

        return new Analysis
        {
            Colors = counts.Count,
            Background = background,
            Ink = ink,
            BandInk = bandInk,
            Strong = strong,
            BandStrong = bandStrong
        };
    }
}
'@

function Resolve-Handle([string]$text)
{
    $trimmed = $text.Trim()
    if ($trimmed -match '^0[xX]([0-9a-fA-F]+)$') {
        return [IntPtr]::new([Convert]::ToInt64($Matches[1], 16))
    }
    if ($trimmed -match '^\d+$') {
        return [IntPtr]::new([Int64]::Parse($trimmed))
    }
    throw "invalid window handle: $text"
}

$handle = Resolve-Handle $Hwnd
if (-not [TermoleWindowCapture]::IsWindow($handle)) {
    throw "window handle is not a valid window: $Hwnd"
}

if ($Width -le 0 -or $Height -le 0) {
    $rectangle = New-Object TermoleWindowCapture+RECT
    if (-not [TermoleWindowCapture]::GetWindowRect($handle, [ref]$rectangle)) {
        throw "GetWindowRect failed for $Hwnd"
    }
    if ($Width -le 0) { $Width = $rectangle.Right - $rectangle.Left }
    if ($Height -le 0) { $Height = $rectangle.Bottom - $rectangle.Top }
}
if ($Width -le 0 -or $Height -le 0) {
    throw "window has no capturable size (${Width}x${Height})"
}

if ($BandTop -ge 0 -and $BandBottom -ge 0) {
    $BandTop = [Math]::Max(0, [Math]::Min($Height - 1, $BandTop))
    $BandBottom = [Math]::Max($BandTop, [Math]::Min($Height - 1, $BandBottom))
}
else {
    $BandTop = -1
    $BandBottom = -1
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$bitmap = [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$device = $graphics.GetHdc()
try {
    $captured = [TermoleWindowCapture]::PrintWindow(
        $handle, $device, [TermoleWindowCapture]::PW_RENDERFULLCONTENT)
}
finally {
    $graphics.ReleaseHdc($device)
    $graphics.Dispose()
}

if (-not $captured) {
    $bitmap.Dispose()
    throw "PrintWindow failed for $Hwnd"
}

$rectangle = [System.Drawing.Rectangle]::new(0, 0, $Width, $Height)
$bits = $bitmap.LockBits(
    $rectangle,
    [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
    [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
try {
    $stride = [Math]::Abs($bits.Stride)
    $pixels = [byte[]]::new($stride * $Height)
    [System.Runtime.InteropServices.Marshal]::Copy($bits.Scan0, $pixels, 0, $pixels.Length)
}
finally {
    $bitmap.UnlockBits($bits)
}

$analysis = [TermoleWindowCapture]::Analyze($pixels, $Width, $Height, $stride, $BandTop, $BandBottom)
$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

$total = [int64]$Width * [int64]$Height
$inkRatio = if ($total -gt 0) { [Math]::Round($analysis.Ink / $total, 6) } else { 0 }
$background = '#{0:x6}' -f $analysis.Background

$stats = [ordered]@{
    captured      = 1
    width         = $Width
    height        = $Height
    colors        = $analysis.Colors
    background    = $background
    inkPixels     = $analysis.Ink
    inkRatio      = $inkRatio
    bandTop       = $BandTop
    bandBottom    = $BandBottom
    bandInkPixels = $analysis.BandInk
    strongPixels  = $analysis.Strong
    bandStrongPixels = $analysis.BandStrong
}

if ($StatsPath) {
    $statsDirectory = Split-Path -Parent $StatsPath
    if ($statsDirectory -and -not (Test-Path -LiteralPath $statsDirectory)) {
        New-Item -ItemType Directory -Force -Path $statsDirectory | Out-Null
    }
    $lines = foreach ($entry in $stats.GetEnumerator()) { "$($entry.Key)=$($entry.Value)" }
    [System.IO.File]::WriteAllLines($StatsPath, [string[]]$lines, [System.Text.Encoding]::ASCII)
}

if (-not $Quiet) {
    Write-Output "captured=$($stats.captured) size=${Width}x${Height} colors=$($analysis.Colors) " +
        "background=$background inkPixels=$($analysis.Ink) inkRatio=$inkRatio " +
        "band=${BandTop}..${BandBottom} bandInkPixels=$($analysis.BandInk) " +
        "strongPixels=$($analysis.Strong) bandStrongPixels=$($analysis.BandStrong)"
    Write-Output "png=$OutputPath"
}
