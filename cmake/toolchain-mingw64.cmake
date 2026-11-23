# MinGW-w64 toolchain for Termole.
#
# Set MINGW_PREFIX to the Windows MinGW-w64 installation before configuring.

set(TERMOLE_MINGW_PREFIX "$ENV{MINGW_PREFIX}" CACHE PATH
    "MinGW-w64 prefix (contains bin/gcc.exe and bin/g++.exe)")

if(NOT TERMOLE_MINGW_PREFIX)
  message(FATAL_ERROR
    "MINGW_PREFIX is not set. Set it to the MinGW-w64 installation or pass "
    "-DTERMOLE_MINGW_PREFIX=<path>.")
endif()

set(CMAKE_C_COMPILER   "${TERMOLE_MINGW_PREFIX}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${TERMOLE_MINGW_PREFIX}/bin/g++.exe")
set(CMAKE_RC_COMPILER  "${TERMOLE_MINGW_PREFIX}/bin/windres.exe")

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static runtime is the default for this tree: no libstdc++/libgcc/winpthread
# DLLs next to the executable or on PATH.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")