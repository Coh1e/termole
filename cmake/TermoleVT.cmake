# Windows embeds these objects in the SDK; POSIX also ships the core as a static target.
function(termole_add_vt target kind)
  set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  add_library(${target} ${kind} "${root}/src/vt/termole_vt.c" "${root}/src/core/input.c")
  target_include_directories(${target} PUBLIC "${root}/include")
  target_compile_features(${target} PRIVATE c_std_11)
  target_link_libraries(${target} PUBLIC ghostty_vt)
endfunction()
