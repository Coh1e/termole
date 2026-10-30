@echo off
setlocal
if not defined TERMOLE_VCVARS set "TERMOLE_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%TERMOLE_VCVARS%"
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0\..\.."
cmake -S spikes/picker -B .local/build/picker -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_BASE_DIR=%CD%/.local/fetchcontent %*
if errorlevel 1 exit /b %errorlevel%
cmake --build .local/build/picker
exit /b %errorlevel%
