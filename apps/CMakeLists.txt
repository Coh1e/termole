function(add_termole_window_app target output_name smoke_enabled cli_enabled)
  if(smoke_enabled)
    set(smoke_source common/smoke.cpp)
  else()
    set(smoke_source common/smoke_noop.cpp)
  endif()
  if(cli_enabled)
    set(cli_sources common/cli.cpp common/config.cpp common/console.cpp common/diagnostics.cpp)
  else()
    set(cli_sources)
  endif()
  add_executable(${target} ${ARGN}
    ${smoke_source}
    ${cli_sources}
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
  if(cli_enabled)
    target_compile_definitions(${target} PRIVATE TERMOLE_VERSION="${PROJECT_VERSION}")
  endif()
endfunction()

# Shipped entry point: one console launcher + internal GUI role, one DLL.
add_termole_window_app(termole_app "termole" OFF ON
  standalone/main.cpp standalone/gui.cpp)

# Test-only copies exercise the real entry point with a smoke driver. They are
# not part of the product package.
if(BUILD_TESTING)
  add_termole_window_app(termole_smoke_app "termole-smoke" ON ON
    standalone/main.cpp standalone/gui.cpp)
  add_termole_window_app(termole_smoke_host "termole-smoke-host" ON OFF host/main.cpp)
  set_target_properties(termole_smoke_host PROPERTIES WIN32_EXECUTABLE ON)
endif()
