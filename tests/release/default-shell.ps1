param([Parameter(Mandatory)][string]$Package,[Parameter(Mandatory)][string]$Evidence)
$ErrorActionPreference = 'Stop'
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
    Start-Sleep -Milliseconds 900
    & $capture -Hwnd ($window.MainWindowHandle.ToInt64().ToString()) -OutputPath (Join-Path $out 'window.png') -StatsPath (Join-Path $out 'pixels.txt') -Quiet
    $ink = Get-Content -LiteralPath (Join-Path $out 'pixels.txt') -Raw | ConvertFrom-StringData
    if ([long]$ink.inkPixels -lt 500) { throw 'Default shell window has too few text pixels' }
    $fact = [ordered]@{executable=(Get-FileHash -LiteralPath $exe).Hash; shell=$shell; children=$children; launcherExitCode=$launcher.ExitCode; title=$title}
    $fact | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'result.json') -Encoding utf8
    "Default shell evidence: $out"
} finally {
    if ($window -and !$window.HasExited) { [void]$window.CloseMainWindow(); if (!$window.WaitForExit(8000)) { throw 'Default shell window did not close' } }
    $launcher.Dispose()
}
