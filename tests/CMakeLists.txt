add_executable(termole_test_console_helper console_helper.cpp)
target_link_libraries(termole_test_console_helper PRIVATE termole_build_options)
set_target_properties(termole_test_console_helper PROPERTIES
  OUTPUT_NAME "termole-test-helper"
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_executable(termole_cli_report_helper cli_report_helper.cpp)
target_link_libraries(termole_cli_report_helper PRIVATE termole_build_options)
set_target_properties(termole_cli_report_helper PROPERTIES
  OUTPUT_NAME "cli-report-helper"
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_executable(termole_test_host_api host_api_test.cpp)
target_link_libraries(termole_test_host_api PRIVATE termole termole_build_options)
add_dependencies(termole_test_host_api termole_test_console_helper termole_smoke_app termole_smoke_host)
set_target_properties(termole_test_host_api PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_executable(termole_t02_host_test t02_host_test.cpp)
target_link_libraries(termole_t02_host_test PRIVATE termole termole_build_options)
add_dependencies(termole_t02_host_test termole_cli_report_helper)
set_target_properties(termole_t02_host_test PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_executable(termole_t03_t05_test t03_t05_test.cpp)
target_link_libraries(termole_t03_t05_test PRIVATE termole termole_build_options)
add_dependencies(termole_t03_t05_test termole_cli_report_helper)
set_target_properties(termole_t03_t05_test PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_executable(termole_terminal_unicode_test
  terminal_unicode_test.cpp
  "${PROJECT_SOURCE_DIR}/src/core/terminal.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/utf8.cpp"
)
target_include_directories(termole_terminal_unicode_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(termole_terminal_unicode_test PRIVATE termole_build_options)

add_executable(termole_cli_no_console_test cli_no_console_test.cpp)
target_link_libraries(termole_cli_no_console_test PRIVATE termole_build_options)
add_dependencies(termole_cli_no_console_test termole_app termole_cli_report_helper)
set_target_properties(termole_cli_no_console_test PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

find_program(TERMOLE_POWERSHELL NAMES powershell.exe)
find_program(TERMOLE_FZF NAMES fzf.exe)
if(NOT TERMOLE_POWERSHELL)
  message(STATUS "powershell.exe not found: PowerShell CLI launch test is omitted.")
endif()

add_test(NAME t01_host_api COMMAND termole_test_host_api)
set_tests_properties(t01_host_api PROPERTIES
  TIMEOUT 120
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME t02_host_args COMMAND termole_t02_host_test)
set_tests_properties(t02_host_args PROPERTIES
  TIMEOUT 60
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME t03_t05_session COMMAND termole_t03_t05_test)
set_tests_properties(t03_t05_session PROPERTIES
  TIMEOUT 60
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME t08_unicode_internal COMMAND termole_terminal_unicode_test)
set_tests_properties(t08_unicode_internal PROPERTIES
  TIMEOUT 30
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

if(TERMOLE_FZF)
  add_executable(termole_fzf_test fzf_test.cpp)
  target_link_libraries(termole_fzf_test PRIVATE termole termole_build_options)
  add_test(NAME t06_fzf COMMAND termole_fzf_test "${TERMOLE_FZF}")
  set_tests_properties(t06_fzf PROPERTIES
    TIMEOUT 30
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  )
endif()

add_test(NAME t02_cli_args
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_app>"
    "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
    "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-args"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_args.cmake"
)
set_tests_properties(t02_cli_args PROPERTIES TIMEOUT 60)

add_test(NAME t02_cli_help
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_app>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_help.cmake"
)
set_tests_properties(t02_cli_help PROPERTIES TIMEOUT 30)

add_test(NAME t02_cli_error
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_app>"
    "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-errors"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_error.cmake"
)
set_tests_properties(t02_cli_error PROPERTIES TIMEOUT 60)

add_test(NAME t02_cli_cmd
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_app>"
    "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
    "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-cmd"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_cmd.cmake"
)
set_tests_properties(t02_cli_cmd PROPERTIES TIMEOUT 60)

if(TERMOLE_POWERSHELL)
  add_test(NAME t02_cli_powershell
    COMMAND "${CMAKE_COMMAND}"
      "-DAPP=$<TARGET_FILE:termole_app>"
      "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
      "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-ps"
      "-DPOWERSHELL=${TERMOLE_POWERSHELL}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_ps.cmake"
  )
  set_tests_properties(t02_cli_powershell PROPERTIES TIMEOUT 60)

  add_test(NAME t02_cli_parallel
    COMMAND "${CMAKE_COMMAND}"
      "-DAPP=$<TARGET_FILE:termole_app>"
      "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
      "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-parallel"
      "-DPOWERSHELL=${TERMOLE_POWERSHELL}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_parallel.cmake"
  )
  set_tests_properties(t02_cli_parallel PROPERTIES TIMEOUT 60)

  add_test(NAME t03_cli_exit
    COMMAND "${CMAKE_COMMAND}"
      "-DAPP=$<TARGET_FILE:termole_app>"
      "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
      "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-exit"
      "-DPOWERSHELL=${TERMOLE_POWERSHELL}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_exit.cmake"
  )
  set_tests_properties(t03_cli_exit PROPERTIES TIMEOUT 60)
endif()

add_test(NAME t02_cli_no_console
  COMMAND termole_cli_no_console_test
    "$<TARGET_FILE:termole_app>"
    "$<TARGET_FILE:termole_cli_report_helper>"
    "${CMAKE_CURRENT_BINARY_DIR}/no console cwd"
    "${CMAKE_CURRENT_BINARY_DIR}/no-console-report.txt"
)
set_tests_properties(t02_cli_no_console PROPERTIES TIMEOUT 60)

add_test(NAME t01_standalone_smoke
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_smoke_app>"
    "-DRESULT=${CMAKE_CURRENT_BINARY_DIR}/standalone-smoke.txt"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.cmake"
)
set_tests_properties(t01_standalone_smoke PROPERTIES TIMEOUT 60)

add_test(NAME t01_host_smoke
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_smoke_host>"
    "-DRESULT=${CMAKE_CURRENT_BINARY_DIR}/host-smoke.txt"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.cmake"
)
set_tests_properties(t01_host_smoke PROPERTIES TIMEOUT 60)
