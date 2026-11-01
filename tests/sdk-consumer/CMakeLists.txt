cmake_minimum_required(VERSION 3.28)
project(TermoleSdkConsumer LANGUAGES CXX)

# This project is intentionally independent from the Termole source tree. It is
# configured by tests/run_sdk_consumer.ps1 against an installed development
# package, so it can only consume the public headers, import library and DLL.
if(NOT DEFINED TERMOLE_PACKAGE_ROOT)
  message(FATAL_ERROR "TERMOLE_PACKAGE_ROOT must point at the installed package")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(Termole CONFIG REQUIRED PATHS "${TERMOLE_PACKAGE_ROOT}" NO_DEFAULT_PATH)

get_target_property(termole_includes Termole::termole INTERFACE_INCLUDE_DIRECTORIES)
if(NOT termole_includes)
  message(FATAL_ERROR "Termole::termole does not publish include directories")
endif()
foreach(directory IN LISTS termole_includes)
  cmake_path(IS_PREFIX TERMOLE_PACKAGE_ROOT "${directory}" inside_package)
  if(NOT inside_package)
    message(FATAL_ERROR "public include directory '${directory}' is outside the package")
  endif()
endforeach()

get_target_property(termole_location Termole::termole IMPORTED_LOCATION_RELEASE)
if(NOT termole_location)
  get_target_property(termole_location Termole::termole IMPORTED_LOCATION)
endif()
if(NOT termole_location)
  message(FATAL_ERROR "Termole::termole does not publish an imported DLL location")
endif()
cmake_path(IS_PREFIX TERMOLE_PACKAGE_ROOT "${termole_location}" dll_inside_package)
if(NOT dll_inside_package)
  message(FATAL_ERROR "imported DLL '${termole_location}' is outside the package")
endif()

add_executable(termole-sdk-consumer main.cpp capture.cpp)
target_compile_definitions(termole-sdk-consumer PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00
)
if(MSVC)
  target_compile_options(termole-sdk-consumer PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()
target_link_libraries(termole-sdk-consumer PRIVATE Termole::termole windowscodecs ole32)

message(STATUS "Termole package: ${TERMOLE_PACKAGE_ROOT}")
message(STATUS "Termole DLL: ${termole_location}")
