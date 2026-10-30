@echo off
setlocal
if not defined TERMOLE_VCVARS set "TERMOLE_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%TERMOLE_VCVARS%"
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0\..\.."
cmake -S spikes/core/minimal -B .local/build/core-minimal -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_CONTOUR=%CD%/.local/deps/contour-stable -DFETCHCONTENT_SOURCE_DIR_GSL=%CD%/.local/deps/gsl -DFETCHCONTENT_SOURCE_DIR_BOXED=%CD%/.local/deps/boxed-cpp -DFETCHCONTENT_SOURCE_DIR_REFLECTION=%CD%/.local/deps/reflection-cpp -DFETCHCONTENT_SOURCE_DIR_UNICODE=%CD%/.local/deps/libunicode
if errorlevel 1 exit /b %errorlevel%
cmake --build .local/build/core-minimal --target termole_core_probe --parallel 4
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir .local/build/core-minimal --output-on-failure
exit /b %errorlevel%
