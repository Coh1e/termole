@echo off
setlocal
if "%~1"=="" (
  echo usage: scripts\test-one.cmd ^<build-target^> [ctest-regex]
  exit /b 2
)
if not defined TERMOLE_VCVARS set "TERMOLE_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%TERMOLE_VCVARS%"
if errorlevel 1 exit /b %errorlevel%
if not defined TERMOLE_PRESET set "TERMOLE_PRESET=windows"
cd /d "%~dp0.."
cmake --build --preset %TERMOLE_PRESET% --target %1
if errorlevel 1 exit /b %errorlevel%
if "%~2"=="" (
  ctest --preset %TERMOLE_PRESET% --output-on-failure
) else (
  ctest --preset %TERMOLE_PRESET% -R %2 --output-on-failure
)
exit /b %errorlevel%
