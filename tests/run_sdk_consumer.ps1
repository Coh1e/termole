param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Helper,
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$Config,
    [Parameter(Mandatory = $true)][string]$Cxx,
    [Parameter(Mandatory = $true)][string]$Ninja,
    [Parameter(Mandatory = $true)][string]$Cmake
)

$ErrorActionPreference = 'Stop'

# The external consumer and the shipped example are compiled at test time. The
# documented entry point (build.cmd) loads vcvars, so only a direct ctest run
# from a plain shell can miss the compiler environment.
if ([string]::IsNullOrEmpty($env:INCLUDE) -or [string]::IsNullOrEmpty($env:LIB)) {
    Write-Host 'SKIP: MSVC environment is not loaded; run build.cmd'
    exit 77
}
if (-not (Test-Path -LiteralPath $Cxx)) {
    Write-Host "SKIP: C++ compiler '$Cxx' is not available"
    exit 77
}
if ([string]::IsNullOrEmpty($Root)) {
    throw 'Root must not be empty'
}

function Invoke-Cmake {
    param([string]$Label, [string[]]$Arguments)
    $log = Join-Path $Root "$Label.log"
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Cmake @Arguments 2>&1 | Out-File -FilePath $log -Encoding utf8
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    if ($code -ne 0) {
        Write-Host "--- $Label exited with $code ---"
        Get-Content -LiteralPath $log | Write-Host
        throw "$Label failed with exit code $code"
    }
}

try {
    $package = Join-Path $Root 'package'
    $consumerBuild = Join-Path $Root 'consumer-build'
    $exampleBuild = Join-Path $Root 'example-build'
    $artifacts = Join-Path $Root 'artifacts'

    if (Test-Path -LiteralPath $Root) {
        Remove-Item -LiteralPath $Root -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Root | Out-Null

    Write-Host "Installing the development package into $package"
    Invoke-Cmake -Label 'install' -Arguments @('--install', $BuildDir, '--prefix', $package)

    $required = @(
        'bin\termole.dll',
        'include\termole\termole.h',
        'include\termole\termole.hpp',
        'lib\termole.lib',
        'lib\cmake\termole\termoleConfig.cmake',
        'lib\cmake\termole\termoleConfigVersion.cmake',
        'lib\cmake\termole\termoleTargets.cmake',
        'share\termole\examples\sdk-host\CMakeLists.txt',
        'share\termole\examples\sdk-host\main.cpp',
        'share\termole\examples\sdk-host\README.md',
        'docs\sdk.md',
        'docs\host-api.md'
    )
    foreach ($relative in $required) {
        $path = Join-Path $package $relative
        if (-not (Test-Path -LiteralPath $path)) {
            throw "the installed package is missing $relative"
        }
    }

    Write-Host 'Configuring the independent consumer against the package'
    Invoke-Cmake -Label 'consumer-configure' -Arguments @(
        '-S', (Join-Path $SourceDir 'tests\sdk-consumer'),
        '-B', $consumerBuild,
        '-G', $Generator,
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DCMAKE_PREFIX_PATH=$package",
        "-DTERMOLE_PACKAGE_ROOT=$package",
        "-DCMAKE_CXX_COMPILER=$Cxx",
        "-DCMAKE_MAKE_PROGRAM=$Ninja",
        '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
        '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF'
    )
    Invoke-Cmake -Label 'consumer-build' -Arguments @('--build', $consumerBuild)
    $consumer = Join-Path $consumerBuild 'termole-sdk-consumer.exe'
    if (-not (Test-Path -LiteralPath $consumer)) {
        throw 'the external consumer executable was not produced'
    }

    Write-Host 'Building the shipped sdk-host example against the package'
    $exampleSource = Join-Path $package 'share\termole\examples\sdk-host'
    Invoke-Cmake -Label 'example-configure' -Arguments @(
        '-S', $exampleSource,
        '-B', $exampleBuild,
        '-G', $Generator,
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DCMAKE_PREFIX_PATH=$package",
        "-DCMAKE_CXX_COMPILER=$Cxx",
        "-DCMAKE_MAKE_PROGRAM=$Ninja",
        '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF'
    )
    Invoke-Cmake -Label 'example-build' -Arguments @('--build', $exampleBuild)
    $example = Join-Path $exampleBuild 'termole-sdk-host.exe'
    if (-not (Test-Path -LiteralPath $example)) {
        throw 'the shipped sdk-host example was not produced'
    }

    Write-Host 'Running the external consumer against the installed DLL'
    $env:PATH = (Join-Path $package 'bin') + ';' + $env:PATH
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    # The CTest working directory is the build's bin directory, which also
    # contains a termole.dll. Run from the consumer directory so the loader
    # cannot pick that DLL through the current-directory search entry.
    Push-Location $consumerBuild
    try {
        $consumerOutput = & $consumer --helper $Helper --artifacts $artifacts `
            --expected-dll (Join-Path $package 'bin\termole.dll') 2>&1
        $consumerCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    $ErrorActionPreference = $previousPreference
    $consumerOutput | Out-File -FilePath (Join-Path $Root 'consumer-run.log') -Encoding utf8
    $consumerOutput | Write-Host
    if ($consumerCode -ne 0) {
        throw "the external consumer failed with exit code $consumerCode"
    }

    foreach ($name in @('t22-sdk-view.png', 't22-sdk-host.png',
                        't22-sdk-view.stats.txt', 't22-sdk-host.stats.txt')) {
        $path = Join-Path $artifacts $name
        if (-not (Test-Path -LiteralPath $path)) {
            throw "the consumer did not produce $name"
        }
    }
    $viewStats = Get-Content -LiteralPath (Join-Path $artifacts 't22-sdk-view.stats.txt') -Raw
    if ($viewStats -notmatch 'printWindow=true') {
        throw 'the view capture did not use PrintWindow successfully'
    }
    if ($viewStats -notmatch 'inkPixels=(\d+)') {
        throw 'the view capture stats are missing inkPixels'
    }

    Write-Host "PASS: external host built from $package and embedded the terminal"
    exit 0
}
catch {
    Write-Host "FAIL: $($_.Exception.Message)"
    exit 1
}
