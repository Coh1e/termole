@echo off
setlocal
if not defined TERMOLE_VCVARS set "TERMOLE_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%TERMOLE_VCVARS%"
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
cmake --preset windows
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows
if errorlevel 1 exit /b %errorlevel%
ctest --preset windows
if errorlevel 1 exit /b %errorlevel%
if exist ".local\package" rmdir /s /q ".local\package"
cmake --install ".local\build\windows" --prefix ".local\package"
if errorlevel 1 exit /b %errorlevel%
cpack --config ".local\build\windows\CPackConfig.cmake"
exit /b %errorlevel%
