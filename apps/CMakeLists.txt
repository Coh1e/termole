function(add_termole_window_app target output_name source smoke_enabled)
  add_executable(${target} WIN32
    ${source}
    common/smoke.cpp
  )
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${output_name}"
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
  )
  target_link_libraries(${target} PRIVATE termole termole_build_options)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  if(smoke_enabled)
    target_compile_definitions(${target} PRIVATE TERMOLE_ENABLE_SMOKE=1)
  endif()
endfunction()

# Shipped entry points: no test-only environment hook is compiled in.
add_termole_window_app(termole_app "termole" standalone/main.cpp OFF)
add_termole_window_app(termole_host "termole-host" host/main.cpp OFF)

# Test-only copies exercise the real entry points with a smoke driver. They are
# not part of the product package.
if(BUILD_TESTING)
  add_termole_window_app(termole_smoke_app "termole-smoke" standalone/main.cpp ON)
  add_termole_window_app(termole_smoke_host "termole-smoke-host" host/main.cpp ON)
endif()
