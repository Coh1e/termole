param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Root
)

$ErrorActionPreference = 'Stop'
$script = Join-Path $Package 'share/termole/examples/select-file.ps1'
$previousPath = $env:PATH
$previousOptions = $env:FZF_DEFAULT_OPTS
$previousOptionsFile = $env:FZF_DEFAULT_OPTS_FILE
$previousTmp = $env:TMP
$previousQuery = $env:TERMOLE_TEST_QUERY
$fixture = [IO.Directory]::CreateDirectory((Join-Path $Root ([Guid]::NewGuid().ToString()))).FullName
# Drive the packaged launcher with a deterministic fzf stand-in. The visual test drives real fzf.
Add-Type -OutputType ConsoleApplication -OutputAssembly (Join-Path $fixture 'fzf.exe') -TypeDefinition @"
using System;
using System.Text;
using System.IO;
class FileSelectorFixture {
    static int Main() {
        if (!String.IsNullOrEmpty(Environment.GetEnvironmentVariable("FZF_DEFAULT_OPTS")) ||
            !String.IsNullOrEmpty(Environment.GetEnvironmentVariable("FZF_DEFAULT_OPTS_FILE"))) return 67;
        Console.InputEncoding = new UTF8Encoding(false);
        Console.OutputEncoding = new UTF8Encoding(false);
        string query = Environment.GetEnvironmentVariable("TERMOLE_TEST_QUERY");
        string line;
        string expected = "file\t" + query;
        bool found = false;
        while ((line = Console.ReadLine()) != null) if (line == expected) found = true;
        switch (query) {
            case "cleanup":
                string index = Environment.GetEnvironmentVariable("TERMOLE_PICKER_INDEX");
                File.SetAttributes(index, FileAttributes.ReadOnly);
                Console.WriteLine("file\t" + File.ReadAllLines(index)[0]);
                return 0;
            case "cancel": return 130;
            case "error": return 2;
            case "failure": return 71;
            case "invalid": Console.WriteLine("file\t../missing"); return 0;
            case "multi": Console.WriteLine("query\nresult"); return 0;
        }
        if (!found) return 1;
        Console.WriteLine(expected);
        return 0;
    }
}
"@
$env:TMP = $fixture
$env:PATH = $fixture + ';' + $env:PATH
$env:FZF_DEFAULT_OPTS = '--multi --print-query'
$env:FZF_DEFAULT_OPTS_FILE = Join-Path $fixture 'options'
[IO.File]::WriteAllText($env:FZF_DEFAULT_OPTS_FILE, '--print0')
$folder = Join-Path $Root 'specified [folder]'
$nested = Join-Path $folder 'nested'
[void][IO.Directory]::CreateDirectory($nested)
$localName = 'local file.txt'
$otherName = [string][char]0xfeff + [char]0x96ea + ' file.txt'
[IO.File]::WriteAllText((Join-Path $Root $localName), '')
[IO.File]::WriteAllText((Join-Path $folder $otherName), '')
[IO.File]::WriteAllText((Join-Path $nested 'descendant.txt'), '')
Push-Location -LiteralPath $Root
try {
    # Inject a directory enumeration failure without modifying the user's ACLs.
    & {
        function Get-ChildItem { throw [UnauthorizedAccessException]::new('enumeration denied') }
        $savedError = [Console]::Error
        $diagnostic = [IO.StringWriter]::new()
        try {
            [Console]::SetError($diagnostic)
            $result = @(& $script $folder)
            if ($LASTEXITCODE -ne 2 -or $result.Count -ne 0 -or $diagnostic.ToString() -notmatch 'enumeration denied') {
                throw "Enumeration failure returned '$result' ($LASTEXITCODE), stderr '$diagnostic'"
            }
        } finally {
            [Console]::SetError($savedError)
        }
    }
    foreach ($case in @(
        @{ Query = $localName; Args = @(); Expected = Join-Path $Root $localName },
        @{ Query = $otherName; Args = @($folder); Expected = Join-Path $folder $otherName },
        @{ Query = 'cancel'; Code = 130; Args = @($folder) },
        @{ Query = 'cleanup'; Code = 2; Reason = 'tmp'; Args = @($folder) },
        @{ Query = 'error'; Code = 2; Reason = 'exit code 2'; Args = @($folder) },
        @{ Query = 'failure'; Code = 2; Reason = 'exit code 71'; Args = @($folder) },
        @{ Query = 'invalid'; Code = 2; Reason = 'invalid selection'; Args = @($folder) },
        @{ Query = 'multi'; Code = 2; Reason = 'invalid selection'; Args = @($folder) },
        @{ Query = 'descendant.txt'; Args = @($folder); Expected = $null },
        @{ Query = 'nested'; Args = @($folder); Expected = $null },
        @{ Query = $localName; Args = @($folder); Expected = $null }
    )) {
        $env:TERMOLE_TEST_QUERY = $case.Query
        $arguments = $case.Args
        $savedError = [Console]::Error
        $diagnostic = [IO.StringWriter]::new()
        try {
            [Console]::SetError($diagnostic)
            $result = @(& $script @arguments)
        } finally {
            [Console]::SetError($savedError)
        }
        if ($case.Reason -and $diagnostic.ToString() -notmatch $case.Reason) {
            throw "Missing diagnostic for $($case.Query): $diagnostic"
        }
        if (!$case.Reason -and $diagnostic.ToString()) { throw "Unexpected diagnostic: $diagnostic" }
        $expectedCode = if ($case.Code) { $case.Code } else { 1 }
        if ($case.Expected) {
            if ($LASTEXITCODE -ne 0 -or $result.Count -ne 1 -or $result[0] -cne $case.Expected) {
                throw "Query '$($case.Query)' returned '$result' ($LASTEXITCODE), expected '$($case.Expected)'"
            }
        } elseif ($result.Count -ne 0 -or $LASTEXITCODE -ne $expectedCode) {
            throw "Fixture case '$($case.Query)' returned '$result' ($LASTEXITCODE)"
        }
    }
} finally {
    $env:TMP = $previousTmp
    Get-ChildItem -LiteralPath $fixture -File -Filter '*.tmp' | ForEach-Object {
        $_.IsReadOnly = $false
        [IO.File]::Delete($_.FullName)
    }
    $env:TERMOLE_TEST_QUERY = $previousQuery
    $env:PATH = $previousPath
    $env:FZF_DEFAULT_OPTS = $previousOptions
    $env:FZF_DEFAULT_OPTS_FILE = $previousOptionsFile
    Pop-Location
}
