param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Helper,
  [string]$Fzf = ''
)

$OutputEncoding = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# The result must already be complete when the pipeline expression returns,
# which only works because termole.exe is a console launcher that waits.
$lines = @('alpha', 'beta')
$result = $lines | & $Exe --stdio $Helper --copy-stdin
if ($LASTEXITCODE -ne 0) {
  Write-Error "PowerShell --stdio pipeline exited with $LASTEXITCODE"
  exit 1
}
$joined = ($result -join "`n")
if ($joined -ne ($lines -join "`n")) {
  Write-Error "PowerShell --stdio result mismatch: '$joined'"
  exit 1
}

# The program's status is the launcher's status.
$null = $lines | & $Exe --stdio $Helper --copy-stdin --exit-code 7
if ($LASTEXITCODE -ne 7) {
  Write-Error "PowerShell --stdio did not forward exit code 7 (got $LASTEXITCODE)"
  exit 1
}

# A larger real pipeline: every line must survive and stay in order.
$many = 1..8000 | ForEach-Object { "line-$_" }
$all = $many | & $Exe --stdio $Helper --copy-stdin
if ($LASTEXITCODE -ne 0) {
  Write-Error "large PowerShell --stdio pipeline exited with $LASTEXITCODE"
  exit 1
}
if (($all | Measure-Object).Count -ne 8000) {
  Write-Error "large PowerShell --stdio pipeline lost lines (got $(($all | Measure-Object).Count))"
  exit 1
}
if ($all[0] -ne 'line-1' -or $all[7999] -ne 'line-8000') {
  Write-Error "large PowerShell --stdio pipeline changed the order"
  exit 1
}

# The caller closes the result pipe early: the pipeline must end instead of
# blocking on the remaining output.
$big = 1..20000 | ForEach-Object { "line-$_" }
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$first = $big | & $Exe --stdio $Helper --copy-stdin | Select-Object -First 1
if ($stopwatch.Elapsed.TotalSeconds -gt 20) {
  Write-Error "early-closed --stdio pipeline did not finish"
  exit 1
}
if ($first -ne 'line-1') {
  Write-Error "early-closed --stdio pipeline lost ordering: '$first'"
  exit 1
}

# T16: real CMD binary redirection. NUL bytes and every byte value must survive;
# `type` is text-only, so this uses `--stdio ... < in > out`, which is still the
# public launcher entry.
$binaryInput = Join-Path $env:TEMP ("termole-t16-binary-in-" + [guid]::NewGuid().ToString('N') + ".bin")
$binaryOutput = Join-Path $env:TEMP ("termole-t16-binary-out-" + [guid]::NewGuid().ToString('N') + ".bin")
try {
  $bytes = [byte[]]::new(131072)
  for ($index = 0; $index -lt $bytes.Length; $index++) {
    $bytes[$index] = [byte]($index % 256)
  }
  $special = [Text.Encoding]::UTF8.GetBytes("中文-🐭`r`n`n")
  $payload = [byte[]]::new($bytes.Length + $special.Length)
  [Array]::Copy($bytes, 0, $payload, 0, $bytes.Length)
  [Array]::Copy($special, 0, $payload, $bytes.Length, $special.Length)
  [IO.File]::WriteAllBytes($binaryInput, $payload)

  $command = "`"$Exe`" --stdio `"$Helper`" --copy-stdin < `"$binaryInput`" > `"$binaryOutput`""
  & cmd.exe /d /s /c $command
  if ($LASTEXITCODE -ne 0) {
    Write-Error "binary --stdio roundtrip exited with $LASTEXITCODE"
    exit 1
  }
  $inputHash = (Get-FileHash -LiteralPath $binaryInput -Algorithm SHA256).Hash
  $outputHash = (Get-FileHash -LiteralPath $binaryOutput -Algorithm SHA256).Hash
  if ($inputHash -ne $outputHash) {
    Write-Error "binary --stdio roundtrip changed NUL/Unicode bytes"
    exit 1
  }
} finally {
  Remove-Item -LiteralPath $binaryInput, $binaryOutput -ErrorAction SilentlyContinue
}

# T16: fzf is only an external example. Its --filter byte output and status must
# pass through unchanged, including --multi as an ordinary program argument.
if ($Fzf -and (Test-Path -LiteralPath $Fzf)) {
  $candidates = @('alpha', 'beta', '中文-🐭', 'zeta')

  $direct = @($candidates | & $Fzf --filter=alp)
  $directExit = $LASTEXITCODE
  $via = @($candidates | & $Exe --stdio $Fzf --filter=alp)
  $viaExit = $LASTEXITCODE
  if ($directExit -ne 0 -or $viaExit -ne 0) {
    Write-Error "fzf --filter status changed (direct $directExit, via $viaExit)"
    exit 1
  }
  if (($direct -join "`n") -ne ($via -join "`n")) {
    Write-Error "fzf --filter output changed: direct '$($direct -join '|')', via '$($via -join '|')'"
    exit 1
  }
  $escape = [char]27
  if (($via -join "`n").Contains($escape)) {
    Write-Error "fzf --filter output is polluted with terminal drawing"
    exit 1
  }

  $multiDirect = @($candidates | & $Fzf --filter=a --multi)
  $multiVia = @($candidates | & $Exe --stdio $Fzf --filter=a --multi)
  if (($multiDirect -join "`n") -ne ($multiVia -join "`n")) {
    Write-Error "fzf --multi argument did not pass through unchanged"
    exit 1
  }

  $null = $candidates | & $Exe --stdio $Fzf --filter=zzz
  if ($LASTEXITCODE -ne 1) {
    Write-Error "fzf no-match status is not forwarded (got $LASTEXITCODE)"
    exit 1
  }

  # NUL-delimited candidates (--read0/--print0) with an embedded newline and
  # Emoji: both NUL and LF must survive the --stdio byte channel.
  $nulInput = Join-Path $env:TEMP ("termole-t16-fzf-nul-in-" + [guid]::NewGuid().ToString('N') + ".bin")
  $nulDirect = Join-Path $env:TEMP ("termole-t16-fzf-nul-direct-" + [guid]::NewGuid().ToString('N') + ".bin")
  $nulVia = Join-Path $env:TEMP ("termole-t16-fzf-nul-via-" + [guid]::NewGuid().ToString('N') + ".bin")
  try {
    $entries = @("alpha`nwith newline", 'beta', '中文-🐭', 'zeta')
    $entryBytes = New-Object System.Collections.Generic.List[byte]
    foreach ($entry in $entries) {
      $entryBytes.AddRange([Text.Encoding]::UTF8.GetBytes($entry))
      $entryBytes.Add(0)
    }
    [IO.File]::WriteAllBytes($nulInput, $entryBytes.ToArray())

    $directCommand = "`"$Fzf`" --filter=a --read0 --print0 < `"$nulInput`" > `"$nulDirect`""
    & cmd.exe /d /s /c $directCommand
    if ($LASTEXITCODE -ne 0) {
      Write-Error "direct fzf --read0 filter exited with $LASTEXITCODE"
      exit 1
    }
    $viaCommand = "`"$Exe`" --stdio `"$Fzf`" --filter=a --read0 --print0 < `"$nulInput`" > `"$nulVia`""
    & cmd.exe /d /s /c $viaCommand
    if ($LASTEXITCODE -ne 0) {
      Write-Error "termole --stdio fzf --read0 exited with $LASTEXITCODE"
      exit 1
    }
    $nulDirectHash = (Get-FileHash -LiteralPath $nulDirect -Algorithm SHA256).Hash
    $nulViaHash = (Get-FileHash -LiteralPath $nulVia -Algorithm SHA256).Hash
    if ($nulDirectHash -ne $nulViaHash) {
      Write-Error "termole --stdio changed NUL-delimited fzf output"
      exit 1
    }
    $nulBytes = [IO.File]::ReadAllBytes($nulVia)
    if ($nulBytes.Length -eq 0 -or [Array]::IndexOf($nulBytes, [byte]0) -lt 0 -or
        [Array]::IndexOf($nulBytes, [byte]10) -lt 0) {
      Write-Error "fzf NUL/embedded-newline output is empty or lost its separators"
      exit 1
    }
  } finally {
    Remove-Item -LiteralPath $nulInput, $nulDirect, $nulVia -ErrorAction SilentlyContinue
  }
}

exit 0
