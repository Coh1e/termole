param([Parameter(Mandatory)][string]$Package,[Parameter(Mandatory)][string]$Evidence)
$ErrorActionPreference = 'Stop'
Add-Type -Name ShellWindow -Namespace TermoleTest -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet=System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr FindWindowEx(System.IntPtr parent, System.IntPtr after, string className, string title);
'@
$out = $Evidence
$shellProfileDir = New-Item -ItemType Directory -Path (Join-Path $out 'profile/Termole') -Force
$title = 'Termole default shell ' + [guid]::NewGuid().ToString('N')
[IO.File]::WriteAllText((Join-Path $shellProfileDir.FullName 'config.toml'), ('title = "' + $title + '"'))
$env:APPDATA = $shellProfileDir.Parent.FullName
$env:LOCALAPPDATA = $env:APPDATA
$exe = (Get-Item -LiteralPath (Join-Path $Package 'bin/termole.exe')).FullName
$capture = (Get-Item -LiteralPath "$PSScriptRoot/../../scripts/capture-window.ps1").FullName
$launcher = Start-Process -FilePath $exe -WorkingDirectory $Package -WindowStyle Hidden -PassThru
$window = $null
try {
    if (!$launcher.WaitForExit(10000) -or $launcher.ExitCode -ne 0) { throw 'Default launcher did not return success' }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $window = Get-Process -Name termole -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -eq $exe -and $_.MainWindowTitle -eq $title -and $_.MainWindowHandle -ne [IntPtr]::Zero
        } | Select-Object -First 1
        if (!$window) { Start-Sleep -Milliseconds 100 }
    } until ($window -or [DateTime]::UtcNow -ge $deadline)
    if (!$window) { throw 'No test-owned default shell window' }
    $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($window.Id)" | Select-Object -ExpandProperty Name)
    $shell = @($children | Where-Object { $_ -match '^(cmd|powershell|pwsh)\.exe$' })
    if (!$shell) { throw "Could not identify the child shell: $children" }
    $view = [TermoleTest.ShellWindow]::FindWindowEx($window.MainWindowHandle,[IntPtr]::Zero,'TermoleViewWindow',$null)
    if ($view -eq [IntPtr]::Zero) { throw 'Default shell did not create its terminal View' }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        & $capture -Hwnd ($view.ToInt64().ToString()) -OutputPath (Join-Path $out 'window.png') -StatsPath (Join-Path $out 'pixels.txt') -Quiet
        $ink = Get-Content -LiteralPath (Join-Path $out 'pixels.txt') -Raw | ConvertFrom-StringData
        if ([long]$ink.inkPixels -ge 500) { break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    if ([long]$ink.inkPixels -lt 500) { throw 'The terminal View has too few text pixels. See window.png' }
    $fact = [ordered]@{executable=(Get-FileHash -LiteralPath $exe).Hash; shell=$shell; children=$children; launcherExitCode=$launcher.ExitCode; title=$title}
    $fact | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'result.json') -Encoding utf8
    "Default shell evidence: $out"
} finally {
    if ($window -and !$window.HasExited) { [void]$window.CloseMainWindow(); if (!$window.WaitForExit(8000)) { throw 'Default shell window did not close' } }
    $launcher.Dispose()
}
