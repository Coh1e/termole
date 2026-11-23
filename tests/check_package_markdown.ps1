param([Parameter(Mandatory = $true)][string]$Root)

$ErrorActionPreference = 'Stop'
$rootPath = (Get-Item -LiteralPath $Root).FullName
$rootPrefix = $rootPath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$documents = @(Get-ChildItem -LiteralPath $rootPath -Recurse -File -Filter '*.md')
if ($documents.Count -eq 0) {
    throw "No Markdown files found in $Root"
}

# Recognize inline destinations and reference definitions. Reference links
# share the destination at their definition, so each destination is checked once.
$inlineLink = [regex]'(?<!\\)\]\(\s*(?:<(?<target>[^>]+)>|(?<target>(?:[^\s()]|\([^()]*\))+))(?:\s+(?:"[^"]*"|''[^'']*''|\([^()]*\)))?\s*\)'
$reference = [regex]'^ {0,3}\[[^\]]+\]:\s*(?:<(?<target>[^>]+)>|(?<target>\S+))'
$anchorsByFile = @{}
$linksByFile = @{}

foreach ($document in $documents) {
    $anchors = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $links = [Collections.Generic.List[object]]::new()
    $lines = [IO.File]::ReadAllLines($document.FullName)
    $fence = ''
    $previous = ''
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $line = $lines[$i]
        if ($fence) {
            if ($line -match ('^ {0,3}' + [regex]::Escape($fence) + $fence[0] + '*\s*$')) {
                $fence = ''
            }
            continue
        }
        if ($line -match '^ {0,3}(`{3,}|~{3,})') {
            $fence = $Matches[1]
            $previous = ''
            continue
        }

        $heading = ''
        if ($line -match '^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$') {
            $heading = $Matches[1]
        } elseif ($previous.Trim() -and $line -match '^ {0,3}(=+|-+)\s*$') {
            $heading = $previous.Trim()
        }
        if ($heading) {
            $title = $inlineLink.Replace($heading, ']')
            $title = [Net.WebUtility]::HtmlDecode(($title -replace '<[^>]+>', '' -replace '(?<!\w)_+|_+(?!\w)', ''))
            $slug = $title.ToLowerInvariant() -replace '[^\p{L}\p{M}\p{N}\p{Pc}\p{Pd} ]', '' -replace ' ', '-'
            $anchor = $slug
            for ($suffix = 1; !$anchors.Add($anchor); $suffix++) {
                $anchor = "$slug-$suffix"
            }
        }
        foreach ($explicit in [regex]::Matches($line, '<a\b[^>]*\s(?:id|name)=["'']([^"'']+)["'']')) {
            [void]$anchors.Add($explicit.Groups[1].Value)
        }

        $definition = $reference.Match($line)
        $matchesOnLine = if ($definition.Success) { @($definition) } else { $inlineLink.Matches($line) }
        foreach ($link in $matchesOnLine) {
            $links.Add(@{ Line = $i + 1; Target = $link.Groups['target'].Value })
        }
        $previous = if ($heading -or $definition.Success) { '' } else { $line }
    }
    $anchorsByFile[$document.FullName] = $anchors
    $linksByFile[$document.FullName] = $links
}

$failures = 0
foreach ($document in $documents) {
    foreach ($link in $linksByFile[$document.FullName]) {
        $destination = [Net.WebUtility]::HtmlDecode($link.Target)
        if ($destination -match '^(?:[a-z][a-z0-9+.-]*:|//)') { continue }

        $parts = $destination.Split('#', 2)
        $path = [Uri]::UnescapeDataString(($parts[0] -split '\?', 2)[0])
        $target = if ($path) {
            [IO.Path]::GetFullPath((Join-Path $document.DirectoryName $path))
        } else {
            $document.FullName
        }
        $problem = if (!$target.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            'target is outside the package'
        } elseif (!(Test-Path -LiteralPath $target)) {
            'missing target'
        } elseif ($parts.Length -eq 2 -and $parts[1] -and $target.EndsWith('.md', [StringComparison]::OrdinalIgnoreCase)) {
            $fragment = [Uri]::UnescapeDataString($parts[1])
            if (!$anchorsByFile.ContainsKey($target) -or !$anchorsByFile[$target].Contains($fragment)) {
                "missing anchor #$fragment"
            }
        }
        if ($problem) {
            $failures++
            [Console]::Error.WriteLine(('{0}:{1}: {2}: {3}' -f
                $document.FullName.Substring($rootPrefix.Length), $link.Line, $link.Target, $problem))
        }
    }
}
if ($failures -gt 0) {
    exit 1
}
Write-Output "Checked links in $($documents.Count) installed Markdown files"
