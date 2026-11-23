# Fails if a build entry point mentions Ninja. Termole uses NMake Makefiles.
set(root "${CMAKE_CURRENT_LIST_DIR}/..")
set(files
  "${root}/CMakeLists.txt"
  "${root}/CMakePresets.json"
)
file(GLOB extra "${root}/cmake/*.cmake")
list(APPEND files ${extra})
list(REMOVE_ITEM files "${root}/cmake/CheckNoNinja.cmake")

foreach(file IN LISTS files)
  if(NOT EXISTS "${file}")
    continue()
  endif()
  file(READ "${file}" contents)
  string(TOLOWER "${contents}" lower_contents)
  if(lower_contents MATCHES "ninja")
    message(FATAL_ERROR "Ninja is forbidden; found a reference in ${file}")
  endif()
endforeach()

message(STATUS "CheckNoNinja: build entry points contain no Ninja references")
