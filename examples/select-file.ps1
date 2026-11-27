# Run from the extracted Windows package with fzf.exe on PATH:
#   .\share\termole\examples\select-file.ps1 [-Directory <folder>]
param([string]$Directory = '.')

$ErrorActionPreference = 'Stop'
$originalInputEncoding = [Console]::InputEncoding
$originalEncoding = [Console]::OutputEncoding
$indexFile = $null
$status = 2
$selectedPath = $null
$previousOptions = $env:FZF_DEFAULT_OPTS
$previousOptionsFile = $env:FZF_DEFAULT_OPTS_FILE
$previousOutputEncoding = $global:OutputEncoding
try {
    $folder = if ($Directory) { Get-Item -LiteralPath $Directory -Force -ErrorAction SilentlyContinue }
    if ($folder -isnot [IO.DirectoryInfo]) {
        [Console]::Error.WriteLine("Invalid directory: $Directory")
        exit 2
    }
    $files = @(Get-ChildItem -LiteralPath $folder.FullName -Force -File)
    if ($files.Count -eq 0) {
        [Console]::Error.WriteLine("No files in directory: " + $folder.FullName)
        exit 1
    }
    $fzf = (Get-Command fzf.exe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1).Source
    if (-not $fzf) {
        [Console]::Error.WriteLine('fzf.exe was not found on PATH.')
        exit 2
    }
    $termole = Join-Path $PSScriptRoot '..\..\..\bin\termole.exe'
    # Windows PowerShell 5.1 can prepend the console input encoding's BOM to a native pipe.
    $global:OutputEncoding = New-Object System.Text.UTF8Encoding($false)
    [Console]::InputEncoding = $global:OutputEncoding
    [Console]::OutputEncoding = $global:OutputEncoding
    $preview = Join-Path $PSScriptRoot 'preview-file.ps1'
    $shell = 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command'
    # Only fzf's zero-based row number enters the preview command.
    $indexFile = [IO.Path]::GetTempFileName()
    [IO.File]::WriteAllLines($indexFile, [string[]]$files.Name, $global:OutputEncoding)
    $previewArgs = @('--with-shell', $shell, '--preview', '& $env:TERMOLE_PICKER_PREVIEW {n}')
    $settings = @('--env', "TERMOLE_PICKER_DIRECTORY=$($folder.FullName)", '--env', "TERMOLE_PICKER_PREVIEW=$preview",
        '--env', "TERMOLE_PICKER_INDEX=$indexFile")
    $env:FZF_DEFAULT_OPTS = $null
    $env:FZF_DEFAULT_OPTS_FILE = $null
    # An ASCII field keeps a leading U+FEFF in a name from being consumed as a native stdout BOM.
    $prefix = "file`t"
    $selection = $files |
        ForEach-Object { $prefix + $_.Name } | & $termole @settings --stdio $fzf --no-multi --delimiter '\t' --with-nth '2..' @previewArgs
    $status = $LASTEXITCODE
    if ($status -eq 0) {
        if ($selection -isnot [string] -or !$selection.StartsWith($prefix, [StringComparison]::Ordinal)) {
            throw 'fzf returned an invalid selection.'
        }
        $name = $selection.Substring($prefix.Length)
        if ([Array]::IndexOf([string[]]$files.Name, $name) -lt 0) {
            throw 'fzf returned an invalid selection.'
        }
        $selectedPath = [IO.Path]::Combine($folder.FullName, $name)
    } elseif ($status -notin 1,130) {
        throw "Selector failed with exit code $status."
    }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $status = 2
} finally {
    $env:FZF_DEFAULT_OPTS = $previousOptions
    $env:FZF_DEFAULT_OPTS_FILE = $previousOptionsFile
    $global:OutputEncoding = $previousOutputEncoding

    if ($indexFile) {
        try {
            [IO.File]::Delete($indexFile)
        } catch {
            [Console]::Error.WriteLine($_.Exception.Message)
            $status = 2
        }
    }
}
try {
    if ($status -eq 0) { $selectedPath }
} finally {
    [Console]::InputEncoding = $originalInputEncoding
    [Console]::OutputEncoding = $originalEncoding
}
exit $status
