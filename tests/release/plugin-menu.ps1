#requires -Version 5.1
param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Evidence)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class UI {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
 [DllImport("user32")] public static extern bool SetProcessDPIAware();
 [DllImport("user32")] public static extern bool PrintWindow(IntPtr w,IntPtr dc,uint flags);
 [DllImport("user32")] public static extern bool SetForegroundWindow(IntPtr w);
 [DllImport("user32")] public static extern uint GetWindowThreadProcessId(IntPtr w, out uint process);
 [DllImport("user32")] public static extern bool IsWindowVisible(IntPtr w);
 [DllImport("user32")] public static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int cx,int cy,uint flags);
 [DllImport("user32")] public static extern bool GetWindowRect(IntPtr w,out Rect r);
 [DllImport("user32")] public static extern bool GetClientRect(IntPtr w,out Rect r);
 [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string n);
 [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr w,IntPtr a,string c,string n);
 [DllImport("user32")] public static extern bool PostMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
 [DllImport("user32")] public static extern IntPtr SendMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
 [DllImport("user32")] public static extern int GetMenuItemCount(IntPtr h);
 [DllImport("user32", CharSet=CharSet.Unicode)] public static extern int GetMenuString(IntPtr h,uint i,StringBuilder text,int n,uint flags);
 public static IntPtr Pos(int x,int y) { return (IntPtr)((x & 65535) | (y << 16)); }
}
"@
[void][UI]::SetProcessDPIAware()
$root = New-Item -ItemType Directory -Path $Evidence -ErrorAction Stop
$termole = (Get-Item -LiteralPath (Join-Path $Package 'bin/termole.exe')).FullName
$config = (Get-Item -LiteralPath (Join-Path $Package 'share/termole/examples/markdown-quote.toml')).FullName
$run = [guid]::NewGuid().ToString('N')
$launchers = [Collections.Generic.List[System.Diagnostics.Process]]::new()
$owned = @{}

function Until([scriptblock]$test) {
    for ($i = 0; $i -lt 60; $i++) {
        $value = & $test
        if ($value) { return $value }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out: $test"
}
function Launch([string]$name,[string]$contents,[int]$x) {
    $gridPath = Join-Path $root.FullName ($name + '.grid')
    $env:TERMOLE_AUDIT_GRID = $gridPath
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes(
      '[Console]::OutputEncoding=[Text.UTF8Encoding]::new(); [Console]::Write([char]27 + "[2J" + [char]27 + "[H"); [Console]::Write(''' + $contents + '''); for ($i=0;$i -lt 20;$i++) { [IO.File]::WriteAllText($env:TERMOLE_AUDIT_GRID,([string][Console]::WindowWidth + "," + [Console]::WindowHeight)); Start-Sleep -Milliseconds 150 }; Start-Sleep 90'))
    $title = "$name-$run"
    $arguments = '--config "{0}" --title "{1}" -e powershell.exe -NoProfile -EncodedCommand {2}' -f $config,$title,$encoded
    $log = Join-Path $root.FullName ($name + '.stderr.txt')
    $out = Join-Path $root.FullName ($name + '.stdout.txt')
    $launchers.Add((Start-Process -FilePath $termole -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardError $log -RedirectStandardOutput $out -PassThru))
    $window = Until { $w = [UI]::FindWindow('TermoleStandaloneWindow',$title); if ($w -ne [IntPtr]::Zero) { $w } }
    $owned[$title] = $window
    [void][UI]::SetWindowPos($window,[IntPtr](-1),$x,80,640,420,0x40)
    Start-Sleep -Milliseconds 950
    $view = [UI]::FindWindowEx($window,[IntPtr]::Zero,'TermoleViewWindow',$null)
    if ($view -eq [IntPtr]::Zero) { throw 'View window missing' }
    $grid = Until { if (Test-Path -LiteralPath $gridPath) { $line = [IO.File]::ReadAllText($gridPath); if ($line -match '^[1-9][0-9]*,[1-9][0-9]*$') { $line } } }
    $dimensions = $grid.Split(',')
    $client = New-Object UI+Rect
    if (![UI]::GetClientRect($view,[ref]$client)) { throw 'No terminal client rectangle' }
    [pscustomobject]@{ Window = $window; View = $view; CellWidth = $client.R / [double]$dimensions[0]; CellHeight = $client.B / [double]$dimensions[1] }
}
function Shot([string]$name,$target,[IntPtr]$menu) {
    $bounds = New-Object UI+Rect
    [void][UI]::GetWindowRect($target.Window,[ref]$bounds)
    if ($name -eq '02-second-view-menu') {
        $first = New-Object UI+Rect
        $firstWindow = $a.Window
        if ([UI]::GetWindowRect($firstWindow,[ref]$first)) { $bounds.L = $first.L }
    }
    $bitmap = New-Object Drawing.Bitmap ($bounds.R-$bounds.L),($bounds.B-$bounds.T)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        # Only the test-owned application windows and the menu belong in the record.
        $targets = @($target.Window)
        if ($name -eq '02-second-view-menu' -and $firstWindow -ne [IntPtr]::Zero) {
            $targets += $firstWindow
        }
        foreach ($handle in $targets) {
            $frame = New-Object UI+Rect
            if (![UI]::GetWindowRect($handle,[ref]$frame)) { throw 'No View window bounds' }
            $windowBitmap = New-Object Drawing.Bitmap ($frame.R-$frame.L),($frame.B-$frame.T)
            $wg = [Drawing.Graphics]::FromImage($windowBitmap)
            try {
                $dc = $wg.GetHdc()
                try { if (![UI]::PrintWindow($handle,$dc,2)) { throw 'View PrintWindow failed' } }
                finally { $wg.ReleaseHdc($dc) }
                $graphics.DrawImageUnscaled($windowBitmap,($frame.L-$bounds.L),($frame.T-$bounds.T))
            } finally { $wg.Dispose(); $windowBitmap.Dispose() }
        }
        $popup = New-Object UI+Rect
        if (![UI]::GetWindowRect($menu,[ref]$popup)) { throw 'No menu bounds' }
        $menuBitmap = New-Object Drawing.Bitmap ($popup.R-$popup.L),($popup.B-$popup.T)
        $mg = [Drawing.Graphics]::FromImage($menuBitmap)
        try {
            $mdc = $mg.GetHdc()
            try { if (![UI]::PrintWindow($menu,$mdc,2)) { throw 'Menu PrintWindow failed' } }
            finally { $mg.ReleaseHdc($mdc) }
            $graphics.DrawImageUnscaled($menuBitmap,($popup.L-$bounds.L),($popup.T-$bounds.T))
        } finally { $mg.Dispose(); $menuBitmap.Dispose() }
        $bitmap.Save((Join-Path $root.FullName "$name.png"))
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}
function Select-Text($target,[int]$col,[int]$row) {
    $end = [UI]::Pos([int](($col + 0.45) * $target.CellWidth),[int](($row + 0.45) * $target.CellHeight))
    [void][UI]::PostMessage($target.View,0x201,[IntPtr]1,[UI]::Pos(4,8))
    [void][UI]::PostMessage($target.View,0x200,[IntPtr]1,$end)
    [void][UI]::PostMessage($target.View,0x202,[IntPtr]0,$end)
    Start-Sleep -Milliseconds 200
}
function Open-Menu($target,[string]$name,[bool]$enabled) {
    [void][UI]::SetForegroundWindow($target.Window)
    # Right button up reaches DefWindowProc on the actual View.
    [void][UI]::PostMessage($target.View,0x205,[IntPtr]0,[UI]::Pos(125,45))
    $ownerProcess = [uint32]0
    [void][UI]::GetWindowThreadProcessId($target.Window,[ref]$ownerProcess)
    $menuWindow = Until {
        $w = [IntPtr]::Zero
        $menuProcess = [uint32]0
        while (($w = [UI]::FindWindowEx([IntPtr]::Zero,$w,'#32768',$null)) -ne [IntPtr]::Zero) {
            if ([UI]::IsWindowVisible($w) -and [UI]::GetWindowThreadProcessId($w,[ref]$menuProcess) -and $menuProcess -eq $ownerProcess) { return $w }
        }
    }
    $menuHandle = [UI]::SendMessage($menuWindow,0x1E1,[IntPtr]0,[IntPtr]0)
    $count = [UI]::GetMenuItemCount($menuHandle)
    $labels = for ($i=0; $i -lt $count; $i++) {
        $buffer = [Text.StringBuilder]::new(512)
        [void][UI]::GetMenuString($menuHandle,$i,$buffer,512,0x400)
        $buffer.ToString()
    }
    "$name items: $($labels -join ', ')" | Add-Content (Join-Path $root.FullName 'result.txt')
    Start-Sleep -Milliseconds 400
    Shot $name $target $menuWindow
    if ($labels -notcontains 'Copy') { throw "Missing Copy menu item: $labels" }
    $contains = $labels -contains '复制为 Markdown 引用'
    if ($contains -ne $enabled) { throw "Unexpected menu: $labels" }
    if (!$enabled) {
        [void][UI]::PostMessage($target.Window,0x001F,[IntPtr]0,[IntPtr]0)
        return
    }
    # Use the native menu keyboard route without changing global mouse buttons.

    Start-Sleep -Milliseconds 350
    if ([UI]::IsWindowVisible($menuWindow)) {
        foreach ($key in (@(40) * (1 + [array]::IndexOf([array]$labels, '复制为 Markdown 引用')) + @(13))) {
            [void][UI]::PostMessage($target.Window,0x100,[IntPtr]$key,[IntPtr]1)
            [void][UI]::PostMessage($target.Window,0x101,[IntPtr]$key,[IntPtr]1)
            Start-Sleep -Milliseconds 80
        }
        Start-Sleep -Milliseconds 400
    }
    "menu open after invocation: $([UI]::IsWindowVisible($menuWindow))" | Add-Content (Join-Path $root.FullName 'result.txt')
}
function Check-Paste([string]$name,[string]$expected) {
    $editor = New-Object Windows.Forms.TextBox
    try {
        $editor.Multiline = $true
        [void][UI]::SendMessage($editor.Handle,0x302,[IntPtr]0,[IntPtr]0)
        $received = $editor.Text
        if ($received -cne $expected) {
            "Editor text differs for $name; chars=$($received.Length), quoted=$($received.StartsWith('> '))" | Add-Content (Join-Path $root.FullName 'result.txt')
            throw "Clipboard text differs for $name"
        }
        [IO.File]::WriteAllText((Join-Path $root.FullName "$name.txt"),$received)
        "$name exact editor text verified" | Add-Content (Join-Path $root.FullName 'result.txt')
    } finally { $editor.Dispose() }
}
try {
    $multi = Launch 'Termole plugin multiline' "第一行`r`nsecond line" 30
    Select-Text $multi 10 1
    Open-Menu $multi '01-enabled-menu' $true
    Check-Paste '01-editor' "> 第一行`r`n> second line"
    [void][UI]::PostMessage($multi.Window,0x0010,[IntPtr]0,[IntPtr]0)
    $a = Launch 'Termole plugin window A' '窗口 A' 30
    $b = Launch 'Termole plugin window B' '窗口 B' 715
    Select-Text $a 5 0
    Select-Text $b 5 0
    Open-Menu $b '02-second-view-menu' $true
    Check-Paste '02-editor' '> 窗口 B'
    [void][UI]::PostMessage($a.Window,0x0010,[IntPtr]0,[IntPtr]0)
    [void][UI]::PostMessage($b.Window,0x0010,[IntPtr]0,[IntPtr]0)
    $config = Join-Path $root.FullName 'disabled.toml'
    [IO.File]::WriteAllText($config,"# plugins disabled`r`n")
    $disabled = Launch 'Termole plugin disabled' '窗口 B' 30
    Select-Text $disabled 5 0
    Open-Menu $disabled '03-disabled-menu' $false
} finally {
    foreach ($title in $owned.Keys) {
        $window = [UI]::FindWindow('TermoleStandaloneWindow',$title)
        if ($window -eq [IntPtr]::Zero -or $window -ne $owned[$title]) { continue }
        [void][UI]::PostMessage($window,0x001F,[IntPtr]0,[IntPtr]0)
        [void][UI]::PostMessage($window,0x0010,[IntPtr]0,[IntPtr]0)
    }
    foreach ($launcher in $launchers) {
        if (!$launcher.WaitForExit(8000)) { throw "Plugin test process did not close: $($launcher.Id)" }
        $launcher.Dispose()
    }
}

Write-Output "Verified enabled, per-view and disabled plugin menus in $Evidence"
