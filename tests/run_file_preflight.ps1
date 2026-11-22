param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Root
)

$ErrorActionPreference = 'Stop'
$script = Join-Path $Package 'share/termole/examples/select-file.ps1'
$windowsPowerShell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
[void][IO.Directory]::CreateDirectory($Root)

# A launch attempt fails at the public executable boundary before it can open a window.
$launcher = [IO.File]::Open((Join-Path $Package 'bin/termole.exe'), 'Open', 'Read', 'None')
try {
    function Test-Preflight($Name, $Directory, $Code, $Reason) {
        $stdout = Join-Path $Root "$Name.stdout"
        $stderr = Join-Path $Root "$Name.stderr"
        $process = Start-Process $windowsPowerShell -ArgumentList @(
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-File', ('"{0}"' -f $script), '-Directory', ('"{0}"' -f $Directory)
        ) -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -Wait -PassThru
        $diagnostic = [IO.File]::ReadAllText($stderr).Trim()
        if ($process.ExitCode -ne $Code -or (Get-Item -LiteralPath $stdout).Length -ne 0 -or
            $diagnostic -cne $Reason) {
            throw "$Name returned $($process.ExitCode) and '$diagnostic'. See $stdout and $stderr."
        }
    }
    Test-Preflight blank '' 2 'Invalid directory:'
    $missing = Join-Path $Root 'missing [directory]'
    Test-Preflight missing $missing 2 "Invalid directory: $missing"
    $empty = Join-Path $Root 'no files'
    [void][IO.Directory]::CreateDirectory((Join-Path $empty 'subfolder'))
    Test-Preflight empty $empty 1 "No files in directory: $empty"
    $file = Join-Path $Root 'regular file.txt'
    [IO.File]::WriteAllText($file, '')
    Test-Preflight file $file 2 "Invalid directory: $file"
    $savedPath = $env:PATH
    try {
        $env:PATH = $empty
        Test-Preflight no-fzf $Root 2 'fzf.exe was not found on PATH.'
    } finally {
        $env:PATH = $savedPath
    }
} finally {
    $launcher.Dispose()
}
