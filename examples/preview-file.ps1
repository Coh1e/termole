param([Parameter(Mandatory)][int]$Index)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
# The picker may close while fzf is cancelling this preview.
$indexStream = [IO.File]::Open($env:TERMOLE_PICKER_INDEX, 'Open', 'Read', 'Read, Delete')
$records = [IO.StreamReader]::new($indexStream, [Text.UTF8Encoding]::new($false), $false)
try {
    for ($i = 0; $i -le $Index; $i++) { $name = $records.ReadLine() }
} finally {
    $records.Dispose()
}
$path = [IO.Path]::Combine($env:TERMOLE_PICKER_DIRECTORY, $name)
$limit = 16384
$bytes = New-Object byte[] $limit
$stream = $null
try {
    $stream = [IO.File]::OpenRead($path)
    $count = 0
    while ($count -lt $limit) {
        $read = $stream.Read($bytes, $count, $limit - $count)
        if ($read -eq 0) { break }
        $count += $read
    }
    $truncated = $stream.Length -gt $count
} catch {
    '[File unreadable]'
    return
} finally {
    if ($stream) { $stream.Dispose() }
}
# Flush only at EOF so a character split by the byte limit is omitted.
$encoding = [Text.UTF8Encoding]::new($false, $true)
if ($count -ge 2 -and $bytes[0] -eq 0xff -and $bytes[1] -eq 0xfe) {
    $encoding = [Text.UnicodeEncoding]::new($false, $true, $true)
} elseif ($count -ge 2 -and $bytes[0] -eq 0xfe -and $bytes[1] -eq 0xff) {
    $encoding = [Text.UnicodeEncoding]::new($true, $true, $true)
}
$decoder = $encoding.GetDecoder()
$chars = New-Object char[] $count
try {
    $length = $decoder.GetChars($bytes, 0, $count, $chars, 0, !$truncated)
} catch [Text.DecoderFallbackException] {
    '[Binary file]'
    return
}
$text = [string]::new($chars, 0, $length).TrimStart([char]0xfeff)
if ($text -match '[\x00-\x08\x0b\x0c\x0e-\x1f\x7f-\x9f]') {
    '[Binary file]'
    return
}
if ($truncated) { '[Truncated at 16 KiB]' }
$text
