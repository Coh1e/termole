param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Evidence,
    [Parameter(Mandatory)][string]$Cmake,
    [Parameter(Mandatory)][string]$Generator,
    [Parameter(Mandatory)][string]$MakeProgram,
    [Parameter(Mandatory)][string]$Cc,
    [Parameter(Mandatory)][string]$Cxx
)
$ErrorActionPreference = 'Stop'
$run = New-Item -ItemType Directory -Path $Evidence
# MinGW ld requires an ASCII name for the imported archives. The junction
# points at the very SDK extracted into the Unicode acceptance directory.
$prefix = New-Item -ItemType Junction -Path (Join-Path $run.FullName 'sdk') -Target $Package
$build = Join-Path $run.FullName 'build'
& $Cmake -S "$PSScriptRoot/sdk" -B $build -G $Generator "-DPackage=$($prefix.FullName)" `
    "-DCMAKE_MAKE_PROGRAM=$MakeProgram" "-DCMAKE_C_COMPILER=$Cc" "-DCMAKE_CXX_COMPILER=$Cxx" `
    '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_EXE_LINKER_FLAGS=-static'
if ($LASTEXITCODE -ne 0) { throw 'External SDK configure failed' }
& $Cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'External SDK build failed' }
& (Join-Path $build 'abi3.exe')
if ($LASTEXITCODE -ne 0) { throw 'External C ABI 3 test failed' }
