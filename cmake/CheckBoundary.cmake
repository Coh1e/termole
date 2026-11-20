# Fails if the C core layer (src/vt) gains C++ sources.
set(root "${CMAKE_CURRENT_LIST_DIR}/..")

file(GLOB cpp_files "${root}/src/vt/*.cpp" "${root}/src/vt/*.hpp" "${root}/src/vt/*.hxx")
if(cpp_files)
  message(FATAL_ERROR "src/vt must stay C-only; found: ${cpp_files}")
endif()

file(GLOB c_files "${root}/src/vt/*.c")
if(NOT c_files)
  message(FATAL_ERROR "src/vt has no C sources")
endif()

message(STATUS "CheckBoundary: src/vt is C-only")
