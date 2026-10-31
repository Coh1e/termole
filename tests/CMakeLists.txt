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

add_executable(termole_config_test
  config_test.cpp
  "${PROJECT_SOURCE_DIR}/apps/common/config.cpp"
)
target_include_directories(termole_config_test PRIVATE "${PROJECT_SOURCE_DIR}/apps"
  "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(termole_config_test PRIVATE termole_build_options)

add_executable(termole_cli_no_console_test cli_no_console_test.cpp)
target_link_libraries(termole_cli_no_console_test PRIVATE termole_build_options)
add_dependencies(termole_cli_no_console_test termole_app termole_cli_report_helper)
set_target_properties(termole_cli_no_console_test PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

find_program(TERMOLE_POWERSHELL NAMES powershell.exe)
find_program(TERMOLE_FZF NAMES fzf.exe)
find_program(TERMOLE_WSL NAMES wsl.exe)
find_program(TERMOLE_NVIM NAMES nvim.exe)
find_program(TERMOLE_HELIX NAMES hx.exe helix.exe)
find_program(TERMOLE_LAZYGIT NAMES lazygit.exe)
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

if(TERMOLE_LAZYGIT)
  add_executable(termole_lazygit_test lazygit_test.cpp)
  target_link_libraries(termole_lazygit_test PRIVATE termole termole_build_options)
  add_test(NAME t19_lazygit COMMAND termole_lazygit_test "${TERMOLE_LAZYGIT}")
  set_tests_properties(t19_lazygit PROPERTIES TIMEOUT 60
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  add_test(NAME t19_lazygit_shell COMMAND termole_lazygit_test "${TERMOLE_LAZYGIT}" shell)
  set_tests_properties(t19_lazygit_shell PROPERTIES TIMEOUT 60
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endif()

if(TERMOLE_NVIM OR TERMOLE_HELIX)
  add_executable(termole_editor_test editor_test.cpp)
  target_link_libraries(termole_editor_test PRIVATE termole termole_build_options)
  if(TERMOLE_NVIM)
    add_test(NAME t18_nvim COMMAND termole_editor_test "${TERMOLE_NVIM}" nvim)
    set_tests_properties(t18_nvim PROPERTIES TIMEOUT 60
      WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    add_executable(termole_editor_shell_test editor_shell_test.cpp)
    target_link_libraries(termole_editor_shell_test PRIVATE termole termole_build_options)
    add_test(NAME t18_nvim_shell COMMAND termole_editor_shell_test nvim nvim)
    set_tests_properties(t18_nvim_shell PROPERTIES TIMEOUT 60
      WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  endif()
  if(TERMOLE_HELIX)
    add_test(NAME t18_helix COMMAND termole_editor_test "${TERMOLE_HELIX}" helix)
    set_tests_properties(t18_helix PROPERTIES TIMEOUT 60
      WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    if(TARGET termole_editor_shell_test)
      add_test(NAME t18_helix_shell COMMAND termole_editor_shell_test hx helix)
      set_tests_properties(t18_helix_shell PROPERTIES TIMEOUT 60
        WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    else()
      add_executable(termole_editor_shell_test editor_shell_test.cpp)
      target_link_libraries(termole_editor_shell_test PRIVATE termole termole_build_options)
      add_test(NAME t18_helix_shell COMMAND termole_editor_shell_test hx helix)
      set_tests_properties(t18_helix_shell PROPERTIES TIMEOUT 60
        WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
  endif()
endif()
if(TERMOLE_WSL)
  add_executable(termole_wsl_test wsl_test.cpp)
  target_link_libraries(termole_wsl_test PRIVATE termole termole_build_options)
  add_test(NAME t17_wsl COMMAND termole_wsl_test "${TERMOLE_WSL}")
  set_tests_properties(t17_wsl PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  )
endif()
if(TERMOLE_FZF)
  add_executable(termole_fzf_test fzf_test.cpp)
  target_link_libraries(termole_fzf_test PRIVATE termole termole_build_options)
  add_test(NAME t06_fzf COMMAND termole_fzf_test "${TERMOLE_FZF}")
  set_tests_properties(t06_fzf PROPERTIES
    TIMEOUT 30
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  )
endif()

# Pixel harness (#27). The deterministic alt-screen scenario is part of the
# default suite; the real-fzf scenario registers only when fzf exists (the same
# condition as t06_fzf) and carries the known-red label while issue #26 makes
# the fzf View blank. The default test preset excludes known-red; run
# `ctest --preset windows-visual` to see the reproducible red.
if(TERMOLE_POWERSHELL)
  add_executable(termole_visual_child visual/alt_screen_child.cpp)
  target_link_libraries(termole_visual_child PRIVATE termole_build_options)

  add_executable(termole_visual_test visual/visual_test.cpp)
  target_link_libraries(termole_visual_test PRIVATE termole termole_build_options)
  add_dependencies(termole_visual_test termole_visual_child)

  add_test(NAME visual_alt_screen COMMAND termole_visual_test
    --mode alt-screen
    --child "$<TARGET_FILE:termole_visual_child>"
    --powershell "${TERMOLE_POWERSHELL}"
    --capture "${PROJECT_SOURCE_DIR}/scripts/capture-window.ps1"
    --artifacts "${CMAKE_CURRENT_BINARY_DIR}/visual/alt-screen")
  set_tests_properties(visual_alt_screen PROPERTIES
    TIMEOUT 120
    LABELS "visual"
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  )

  if(TERMOLE_FZF)
    add_test(NAME visual_fzf COMMAND termole_visual_test
      --mode fzf
      --fzf "${TERMOLE_FZF}"
      --powershell "${TERMOLE_POWERSHELL}"
      --capture "${PROJECT_SOURCE_DIR}/scripts/capture-window.ps1"
      --artifacts "${CMAKE_CURRENT_BINARY_DIR}/visual/fzf")
    set_tests_properties(visual_fzf PROPERTIES
      TIMEOUT 120
      LABELS "visual;known-red"
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
  endif()
endif()

add_executable(termole_cmd_test cmd_test.cpp)
target_link_libraries(termole_cmd_test PRIVATE termole termole_build_options)
add_test(NAME t17_cmd_target COMMAND termole_cmd_test)
set_tests_properties(t17_cmd_target PROPERTIES TIMEOUT 30
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_executable(termole_wrap_test wrap_test.cpp)
target_link_libraries(termole_wrap_test PRIVATE termole termole_build_options)
add_test(NAME t17_wrap COMMAND termole_wrap_test)
set_tests_properties(t17_wrap PROPERTIES TIMEOUT 30
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_executable(termole_app_window_test app_window_test.cpp)
target_link_libraries(termole_app_window_test PRIVATE termole_build_options)
add_test(NAME t01_app_window COMMAND termole_app_window_test "$<TARGET_FILE:termole_app>")
set_tests_properties(t01_app_window PROPERTIES TIMEOUT 30
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_executable(termole_soak_test soak_test.cpp)
target_link_libraries(termole_soak_test PRIVATE termole termole_build_options psapi)
add_dependencies(termole_soak_test termole_cli_report_helper)
add_test(NAME t20_soak COMMAND termole_soak_test "$<TARGET_FILE:termole_cli_report_helper>")
set_tests_properties(t20_soak PROPERTIES TIMEOUT 180
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_executable(termole_idle_paint_test idle_paint_test.cpp)
target_link_libraries(termole_idle_paint_test PRIVATE termole termole_build_options)
add_test(NAME t21_idle_paint COMMAND termole_idle_paint_test)
set_tests_properties(t21_idle_paint PROPERTIES TIMEOUT 30
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

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

  add_test(NAME t13_cli_applet
    COMMAND "${CMAKE_COMMAND}"
      "-DAPP=$<TARGET_FILE:termole_app>"
      "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
      "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-applet"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_applet.cmake"
  )
  set_tests_properties(t13_cli_applet PROPERTIES TIMEOUT 60)

  add_test(NAME t14_cli_config
    COMMAND "${CMAKE_COMMAND}"
      "-DAPP=$<TARGET_FILE:termole_app>"
      "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
      "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-config"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_config.cmake"
  )
  set_tests_properties(t14_cli_config PROPERTIES TIMEOUT 60)
endif()

add_executable(termole_launcher_test launcher_test.cpp)
target_link_libraries(termole_launcher_test PRIVATE termole_build_options)
add_dependencies(termole_launcher_test termole_app termole_cli_report_helper)
add_test(NAME t26_launcher COMMAND termole_launcher_test
  "$<TARGET_FILE:termole_app>"
  "$<TARGET_FILE:termole_cli_report_helper>"
  "${CMAKE_CURRENT_BINARY_DIR}/launcher")
set_tests_properties(t26_launcher PROPERTIES TIMEOUT 120
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_test(NAME t26_cli_pipelines
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_app>"
    "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
    "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/launcher-pipelines"
    "-DPOWERSHELL=${TERMOLE_POWERSHELL}"
    "-DFZF=${TERMOLE_FZF}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_pipelines.cmake"
)
set_tests_properties(t26_cli_pipelines PROPERTIES TIMEOUT 180)

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
    "-DAPP_ARGS=--gui"
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
