@echo off
setlocal
if not defined TERMOLE_VCVARS set "TERMOLE_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%TERMOLE_VCVARS%"
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0\..\.."
cmake -S spikes/native-view -B .local/build/native-view -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_BASE_DIR=%CD%/.local/fetchcontent
if errorlevel 1 exit /b %errorlevel%
cmake --build .local/build/native-view
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir .local/build/native-view --output-on-failure
exit /b %errorlevel%
