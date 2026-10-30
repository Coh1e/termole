$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$depsRoot = Join-Path $projectRoot '.local/deps'
New-Item -ItemType Directory -Force $depsRoot | Out-Null
$dependencies = @(
    @('contour-stable', 'https://github.com/contour-terminal/contour.git', '3aef6f7243f571fd4fda2642da190a244bb87a57'),
    @('gsl', 'https://github.com/microsoft/GSL.git', '0f6dbc9e2915ef5c16830f3fa3565738de2a9230'),
    @('boxed-cpp', 'https://github.com/contour-terminal/boxed-cpp.git', '82b362155c4e80d274282b51f1e4750af27e471a'),
    @('reflection-cpp', 'https://github.com/contour-terminal/reflection-cpp.git', '8a83b3259f1be01098f942d525bcead0cfd14a74'),
    @('libunicode', 'https://github.com/contour-terminal/libunicode.git', '25648e888e2433604c3e6f392796910f761d2285')
)
foreach ($dependency in $dependencies) {
    $destination = Join-Path $depsRoot $dependency[0]
    if (-not (Test-Path -LiteralPath $destination)) {
        git init $destination
        if ($LASTEXITCODE -ne 0) { throw "Cannot initialize $destination" }
        git -C $destination remote add origin $dependency[1]
        git -C $destination fetch --depth 1 origin $dependency[2]
        if ($LASTEXITCODE -ne 0) { throw "Cannot download $($dependency[0])" }
        git -C $destination checkout --detach FETCH_HEAD
        if ($LASTEXITCODE -ne 0) { throw "Cannot check out $($dependency[0])" }
    }
    $actual = git -C $destination rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $dependency[2]) {
        throw "Version mismatch at $destination; preserve local changes and resolve manually."
    }
    Write-Output "$($dependency[0]): $actual"
}
Write-Output 'Sources prepared. libunicode may download Unicode 17 data during CMake configure.'
