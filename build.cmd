@echo off
setlocal
if not defined TERMOLE_VCVARS set "TERMOLE_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%TERMOLE_VCVARS%"
if errorlevel 1 exit /b %errorlevel%
if not defined TERMOLE_PRESET set "TERMOLE_PRESET=windows"
cd /d "%~dp0"
cmake --preset %TERMOLE_PRESET% %*
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset %TERMOLE_PRESET%
if errorlevel 1 exit /b %errorlevel%
ctest --preset %TERMOLE_PRESET%
exit /b %errorlevel%
