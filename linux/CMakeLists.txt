cmake_minimum_required(VERSION 3.20)
project(termole_linux_core LANGUAGES C CXX)

# Minimal Linux entry point for the C VT core and POSIX PTY. The Windows
# product build stays in the top-level CMakeLists.
set(TERMOLE_GHOSTTY_VT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/libghostty-vt/1.0.1-linux"
    CACHE PATH "Linux libghostty-vt prefix (include/ and lib/)")

find_library(TERMOLE_GHOSTTY_VT_LIBRARY
  NAMES libghostty-vt.a ghostty-vt-static.a
  PATHS "${TERMOLE_GHOSTTY_VT_ROOT}/lib" NO_DEFAULT_PATH)
if(NOT TERMOLE_GHOSTTY_VT_LIBRARY)
  message(FATAL_ERROR
    "Linux libghostty-vt.a not found under ${TERMOLE_GHOSTTY_VT_ROOT}/lib. "
    "See docs/cross-platform-build.md.")
endif()

add_library(ghostty_vt STATIC IMPORTED)
set_target_properties(ghostty_vt PROPERTIES
  IMPORTED_LOCATION "${TERMOLE_GHOSTTY_VT_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${TERMOLE_GHOSTTY_VT_ROOT}/include"
  INTERFACE_COMPILE_DEFINITIONS GHOSTTY_STATIC
)

add_library(termole_core_linux STATIC ../src/vt/termole_vt.c)
target_include_directories(termole_core_linux PUBLIC ../include ../src)
target_compile_features(termole_core_linux PRIVATE c_std_11)
target_link_libraries(termole_core_linux PUBLIC ghostty_vt m pthread)

add_executable(vt_adapter_linux ../tests/vt_adapter_test.c)
target_link_libraries(vt_adapter_linux PRIVATE termole_core_linux)

add_executable(posix_session_linux
  ../tests/posix_session_test.cpp
  ../src/platform/posix_pty.cpp
  ../src/platform/posix_session.cpp
  ../src/platform/pty.cpp
)
target_include_directories(posix_session_linux PRIVATE ../src)
target_compile_features(posix_session_linux PRIVATE cxx_std_20)
target_link_libraries(posix_session_linux PRIVATE termole_core_linux util)

enable_testing()
add_test(NAME vt_adapter_linux COMMAND vt_adapter_linux)
add_test(NAME posix_session_linux COMMAND posix_session_linux)
add_executable(posix_interactive_linux
  ../tests/posix_interactive_test.cpp
  ../src/platform/posix_pty.cpp
  ../src/platform/posix_session.cpp
  ../src/platform/pty.cpp
  ../src/platform/pty.cpp
)
target_include_directories(posix_interactive_linux PRIVATE ../src)
target_compile_features(posix_interactive_linux PRIVATE cxx_std_20)
target_link_libraries(posix_interactive_linux PRIVATE termole_core_linux util)
add_test(NAME posix_interactive_linux COMMAND posix_interactive_linux)
