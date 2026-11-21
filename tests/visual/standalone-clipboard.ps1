#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Termole,
    [Parameter(Mandatory)][string]$Artifacts
)

$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Termole).Path
$run = New-Item -ItemType Directory -Path (Join-Path $Artifacts ([guid]::NewGuid().ToString('N'))) -Force
Write-Output "Artifacts: $($run.FullName)"
$capture = Join-Path $PSScriptRoot '../../scripts/capture-window.ps1'
$expected = 'Termole ' + [char]0x4e2d + [char]0x6587 + ' test'
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class T2Input {
    [DllImport("user32")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string name);
    [DllImport("user32")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32")] public static extern uint GetClipboardSequenceNumber();
}
"@
[void][T2Input]::SetThreadDpiAwarenessContext(-4)
function Send-ViewMessage($message, $w, $l) {
    if (!([T2Input]::PostMessage($view, $message, $w, $l))) { throw "PostMessage $message failed" }
}
function Wait-File($path) {
    for ($i = 0; $i -lt 100; $i++) {
        if (Test-Path -LiteralPath $path) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for $path"
}
function Capture-View($name) {
    & $capture -Hwnd $view.ToInt64() -OutputPath "$run/$name.png" -StatsPath "$run/$name.txt" -Quiet
    if ($LASTEXITCODE -ne 0) { throw "Capture failed: $name" }
}

$originalProfile = $env:APPDATA
$originalTemp = $env:TEMP
$originalTmp = $env:TMP
Push-Location -LiteralPath $run.FullName
try {
    $env:APPDATA = (New-Item -ItemType Directory profile).FullName
    @(
        'Set-PSReadLineOption -HistorySaveStyle SaveNothing'
        'Clear-Host'
        'Write-Host (''Termole '' + [char]0x4e2d + [char]0x6587 + '' test'')'
        '[IO.File]::WriteAllLines((Join-Path $PSScriptRoot ''geometry.txt''), [string[]]@([Console]::WindowWidth, [Console]::WindowHeight, $PSVersionTable.PSVersion.ToString()))'
        '$line = Read-Host ''Paste here'''
        '[IO.File]::WriteAllText((Join-Path $PSScriptRoot ''received.txt''), $line)'
    ) | Set-Content foreground.ps1 -Encoding ASCII
    $before = @(Get-Process termole -ErrorAction SilentlyContinue | ForEach-Object Id)
    Start-Process -FilePath $exe -WorkingDirectory $run.FullName -WindowStyle Hidden
    for ($i = 0; $i -lt 100; $i++) {
        $gui = Get-Process termole -ErrorAction SilentlyContinue | Where-Object {
            $_.Id -notin $before -and $_.Path -eq $exe -and $_.MainWindowHandle
        } | Select-Object -First 1
        if ($gui) {
            $view = [T2Input]::FindWindowEx($gui.MainWindowHandle, 0, 'TermoleViewWindow', $null)
            if ($view -ne [IntPtr]::Zero) { break }
        }
        Start-Sleep -Milliseconds 100
    }
    if (!$view -or $view -eq [IntPtr]::Zero) { throw 'Standalone terminal View missing' }
    foreach ($letter in ('& ./foreground.ps1').ToCharArray()) { Send-ViewMessage 258 ([int]$letter) 0 }
    Send-ViewMessage 256 13 0
    Wait-File geometry.txt
    Start-Sleep -Milliseconds 400
    Capture-View before
    $stats = Get-Content before.txt -Raw | ConvertFrom-StringData
    $geometry = Get-Content geometry.txt
    $cellWidth = [double]$stats.width / [int]$geometry[0]
    $cellHeight = [math]::Floor([int]$stats.height / [int]$geometry[1])
    $start = ([int]($cellHeight / 2) -shl 16) + [int]($cellWidth / 2)
    # The two Han characters each occupy two columns. The last t is in column 16 (zero-based).
    # Preserve the fractional glyph advance until the final pixel coordinate.
    $end = ([int]($cellHeight / 2) -shl 16) + [int](16.5 * $cellWidth)
    Send-ViewMessage 513 1 $start
    Send-ViewMessage 512 1 $end
    Send-ViewMessage 514 0 $end
    Start-Sleep -Milliseconds 200
    Capture-View selection
    $sequence = [T2Input]::GetClipboardSequenceNumber()
    Send-ViewMessage 258 3 0
    $copied = $null
    $readError = $null
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 100
        if ([T2Input]::GetClipboardSequenceNumber() -eq $sequence) { continue }
        try {
            $copied = Get-Clipboard -Raw
            if ($copied -ceq $expected) { break }
        } catch {
            $readError = $_.Exception.GetType().Name
        }
    }
    if ($copied -cne $expected) {
        $prefix = 0
        $length = ([string]$copied).Length
        while ($prefix -lt [Math]::Min($length, $expected.Length) -and $copied[$prefix] -ceq $expected[$prefix]) { $prefix++ }
        throw "Copy failed. Clipboard sequence $sequence -> $([T2Input]::GetClipboardSequenceNumber()), expected $($expected.Length) UTF-16 units, received $length, matching prefix $prefix, read error '$readError', viewport $($stats.width)x$($stats.height) for $($geometry[0])x$($geometry[1]) cells."
    }
    Send-ViewMessage 258 22 0
    Start-Sleep -Milliseconds 400
    Capture-View paste
    Send-ViewMessage 256 13 0
    Wait-File received.txt
    $received = [IO.File]::ReadAllText("$run/received.txt")
    if ($received -cne $expected) { throw 'Foreground Read-Host received different text' }
    Write-Output "PASS: Windows $([Environment]::OSVersion.Version), PowerShell $($geometry[2]), exact clipboard and recipient text"
} finally {
    if ($gui) { [void][T2Input]::PostMessage($gui.MainWindowHandle, 16, 0, 0) }
    $env:APPDATA = $originalProfile
    $env:TEMP = $originalTemp
    $env:TMP = $originalTmp
    Pop-Location
}
