cmake_minimum_required(VERSION 3.28)
project(TermoleSdkHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(Termole CONFIG REQUIRED)

add_executable(termole-sdk-host WIN32 main.cpp)
target_compile_definitions(termole-sdk-host PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(termole-sdk-host PRIVATE Termole::termole)
set_target_properties(termole-sdk-host PROPERTIES
  OUTPUT_NAME "termole-sdk-host"
)
