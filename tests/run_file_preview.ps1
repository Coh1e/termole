param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Root)
$ErrorActionPreference = 'Stop'
$preview = Join-Path $Package 'share/termole/examples/preview-file.ps1'
$folder = [IO.Directory]::CreateDirectory($Root).FullName
$record = Join-Path $folder 'current'
$originalEncoding = [Console]::OutputEncoding
$previousIndex = $env:TERMOLE_PICKER_INDEX
$previousDirectory = $env:TERMOLE_PICKER_DIRECTORY
try {
    $env:TERMOLE_PICKER_INDEX = $record
    $env:TERMOLE_PICKER_DIRECTORY = $folder
    $name = [char]0xfeff + 'file %PATH% & [1].txt'
    [IO.File]::WriteAllText($record, $name + "`n")
    [IO.File]::WriteAllText((Join-Path $folder $name), 'visible text')
    $output = @(& $preview 0)
    if ($output.Count -ne 1 -or $output[0] -cne 'visible text') { throw "Unexpected preview: $output" }
    $unicodeText = [string][char]0x96ea
    foreach ($encoding in @([Text.Encoding]::Unicode, [Text.Encoding]::BigEndianUnicode)) {
        [IO.File]::WriteAllText((Join-Path $folder $name), $unicodeText, $encoding)
        if ((& $preview 0) -cne $unicodeText) { throw 'UTF-16 BOM text preview failed' }
    }
    $large = ('a' * 16384) + 'unread tail'
    [IO.File]::WriteAllText((Join-Path $folder $name), $large)
    $output = @(& $preview 0)
    if ($output.Count -ne 2 -or $output[1] -cne ('a' * 16384) -or $output[0] -notmatch 'truncated') {
        throw 'Large file preview did not stop and report its limit'
    }
    [IO.File]::WriteAllBytes((Join-Path $folder $name), [byte[]](65,27,66))
    if ((& $preview 0) -cne '[Binary file]') { throw 'Raw binary appeared in the preview' }
    [IO.File]::WriteAllBytes((Join-Path $folder $name), [byte[]](0xff,65))
    if ((& $preview 0) -cne '[Binary file]') { throw 'Invalid UTF-8 appeared in the preview' }
    $split = ('z' * 16383) + $unicodeText
    [IO.File]::WriteAllText((Join-Path $folder $name), $split)
    $output = @(& $preview 0)
    if ($output.Count -ne 2 -or $output[1] -cne ('z' * 16383)) { throw 'Split character in preview' }
    $locked = [IO.File]::Open((Join-Path $folder $name), 'Open', 'ReadWrite', 'None')
    try {
        if ((& $preview 0) -cne '[File unreadable]') { throw 'Missing unreadable prompt' }
    } finally {
        $locked.Dispose()
    }
} finally {
    [Console]::OutputEncoding = $originalEncoding
    $env:TERMOLE_PICKER_INDEX = $previousIndex
    $env:TERMOLE_PICKER_DIRECTORY = $previousDirectory
}
