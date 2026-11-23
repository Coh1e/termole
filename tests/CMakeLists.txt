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

add_executable(termole_config_test
  config_test.cpp
  "${PROJECT_SOURCE_DIR}/apps/common/config.cpp"
)
target_include_directories(termole_config_test PRIVATE "${PROJECT_SOURCE_DIR}/apps"
  "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(termole_config_test PRIVATE termole_build_options)

add_executable(termole_pipe_helper pipe_helper.cpp)
target_link_libraries(termole_pipe_helper PRIVATE termole_build_options)
set_target_properties(termole_pipe_helper PROPERTIES
  OUTPUT_NAME "pipe-helper"
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_executable(termole_pipe_runner_test
  pipe_runner_test.cpp
  "${PROJECT_SOURCE_DIR}/apps/common/pipe.cpp"
)
target_include_directories(termole_pipe_runner_test PRIVATE "${PROJECT_SOURCE_DIR}/apps")
target_link_libraries(termole_pipe_runner_test PRIVATE termole_build_options)
add_dependencies(termole_pipe_runner_test termole_pipe_helper)
add_test(NAME t25_pipe_runner COMMAND termole_pipe_runner_test)
set_tests_properties(t25_pipe_runner PROPERTIES TIMEOUT 60
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_executable(termole_pipe_e2e_test pipe_e2e_test.cpp)
target_link_libraries(termole_pipe_e2e_test PRIVATE termole termole_build_options)
add_dependencies(termole_pipe_e2e_test termole_app termole_pipe_helper)
add_test(NAME t25_pipe_e2e COMMAND termole_pipe_e2e_test
  "$<TARGET_FILE:termole_app>"
  "$<TARGET_FILE:termole_pipe_helper>"
  "${CMAKE_CURRENT_BINARY_DIR}/pipe-e2e")
set_tests_properties(t25_pipe_e2e PROPERTIES TIMEOUT 120
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

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

# T22: install the current build as a development package, build the shipped
# example and an independent consumer against it, then run the consumer. The
# consumer uses only the public headers, static libraries and records
# pixel evidence for the embedded View.
add_test(NAME t22_sdk_consumer
  COMMAND "${CMAKE_COMMAND}"
    "-DSourceDir=${PROJECT_SOURCE_DIR}"
    "-DBuildDir=${CMAKE_BINARY_DIR}"
    "-DRoot=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer"
    "-DHelper=$<TARGET_FILE:termole_cli_report_helper>"
    "-DGenerator=${CMAKE_GENERATOR}"
    "-DConfig=${CMAKE_BUILD_TYPE}"
    "-DCxx=${CMAKE_CXX_COMPILER}"
    "-DMakeProgram=${CMAKE_MAKE_PROGRAM}"
    "-DLibrary=$<TARGET_FILE_NAME:termole>"
    "-DPowerShell=${TERMOLE_POWERSHELL}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_sdk_consumer.cmake"
)
set_tests_properties(t22_sdk_consumer PROPERTIES
  TIMEOUT 300
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
    SKIP_RETURN_CODE 77
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

# Pixel harness (#27). Both scenarios are part of the default suite: the
# deterministic alt-screen probe and the real-fzf scenario, which registers only
# when fzf exists (the same condition as t06_fzf). Every run writes PNG and stats
# under <build>/tests/visual/; failures keep them for review.
if(TERMOLE_POWERSHELL)
  add_executable(termole_visual_child visual/alt_screen_child.cpp)
  target_link_libraries(termole_visual_child PRIVATE termole_build_options)

  # T12 (#13) controlled target: enables mouse/bracketed-paste modes and reads
  # the real ConPTY input bytes for the mouse/paste acceptance.
  add_executable(termole_input_child visual/t12_input_child.cpp)
  target_link_libraries(termole_input_child PRIVATE termole_build_options)

  add_executable(termole_visual_test visual/visual_test.cpp)
  target_link_libraries(termole_visual_test PRIVATE termole termole_build_options psapi)
  add_dependencies(termole_visual_test termole_visual_child termole_input_child termole_app)

  foreach(mode latency latency-standalone)
    add_test(NAME visual_t21_${mode} COMMAND termole_visual_test
      --mode ${mode}
      --child "$<TARGET_FILE:termole_visual_child>"
      --app "$<TARGET_FILE:termole_app>"
      --powershell "${TERMOLE_POWERSHELL}"
      --capture "${PROJECT_SOURCE_DIR}/scripts/capture-window.ps1"
      --artifacts "${CMAKE_CURRENT_BINARY_DIR}/visual/t21-${mode}")
    set_tests_properties(visual_t21_${mode} PROPERTIES TIMEOUT 120 LABELS "visual" RUN_SERIAL TRUE
      WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  endforeach()

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

  # T12 interaction: mouse/wheel messages through the View path, Shift+Insert
  # and bracketed paste, with a pixel capture at every step.
  add_test(NAME visual_input_interaction COMMAND termole_visual_test
    --mode input-interaction
    --input-child "$<TARGET_FILE:termole_input_child>"
    --powershell "${TERMOLE_POWERSHELL}"
    --capture "${PROJECT_SOURCE_DIR}/scripts/capture-window.ps1"
    --artifacts "${CMAKE_CURRENT_BINARY_DIR}/visual/input-interaction")
  set_tests_properties(visual_input_interaction PROPERTIES
    TIMEOUT 240
    LABELS "visual"
    SKIP_RETURN_CODE 77
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  )

  # Same T12 interactions through the public standalone entry (`termole -e`).
  add_test(NAME visual_input_standalone COMMAND termole_visual_test
    --mode input-standalone
    --input-child "$<TARGET_FILE:termole_input_child>"
    --app "$<TARGET_FILE:termole_app>"
    --powershell "${TERMOLE_POWERSHELL}"
    --capture "${PROJECT_SOURCE_DIR}/scripts/capture-window.ps1"
    --artifacts "${CMAKE_CURRENT_BINARY_DIR}/visual/input-standalone")
  set_tests_properties(visual_input_standalone PROPERTIES
    TIMEOUT 240
    LABELS "visual"
    SKIP_RETURN_CODE 77
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
      ENVIRONMENT_MODIFICATION "NO_COLOR=unset:"
      TIMEOUT 120
      LABELS "visual"
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    # T06 interaction: drives real fzf through the View's WM_KEYDOWN/WM_CHAR
    # path (arrow, typing, Enter, Escape) and captures pixels after each step.
    # No --filter/--no-color and no alt-screen bypass: this is fzf's own UI.
    add_test(NAME visual_fzf_interaction COMMAND termole_visual_test
      --mode fzf-interaction
      --fzf "${TERMOLE_FZF}"
      --powershell "${TERMOLE_POWERSHELL}"
      --capture "${PROJECT_SOURCE_DIR}/scripts/capture-window.ps1"
      --artifacts "${CMAKE_CURRENT_BINARY_DIR}/visual/fzf-interaction")
    set_tests_properties(visual_fzf_interaction PROPERTIES
      ENVIRONMENT_MODIFICATION "NO_COLOR=unset:"
      TIMEOUT 180
      LABELS "visual"
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    # Same interactions through the public standalone entry (`termole -e`).
    # Keys are posted to the app's TermoleViewWindow child; the app has no
    # screen-text API, so the captures are the evidence.
    add_test(NAME visual_fzf_standalone COMMAND termole_visual_test
      --mode fzf-standalone
      --app "$<TARGET_FILE:termole_app>"
      --fzf "${TERMOLE_FZF}"
      --powershell "${TERMOLE_POWERSHELL}"
      --capture "${PROJECT_SOURCE_DIR}/scripts/capture-window.ps1"
      --artifacts "${CMAKE_CURRENT_BINARY_DIR}/visual/fzf-standalone")
    set_tests_properties(visual_fzf_standalone PROPERTIES
      ENVIRONMENT_MODIFICATION "NO_COLOR=unset:"
      TIMEOUT 240
      LABELS "visual"
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
target_link_libraries(termole_idle_paint_test PRIVATE termole termole_build_options psapi)
add_test(NAME t21_idle_paint COMMAND termole_idle_paint_test)
set_tests_properties(t21_idle_paint PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
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

add_test(NAME t26_cli_parallel
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_app>"
    "-DHELPER=$<TARGET_FILE:termole_cli_report_helper>"
    "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-parallel"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_parallel.cmake"
)
set_tests_properties(t26_cli_parallel PROPERTIES TIMEOUT 60)

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

# MinGW: console tests use wmain; enable the Unicode startup entry point.
set(_termole_wmain_targets
  termole_test_console_helper
  termole_cli_report_helper
  termole_pipe_helper
  termole_pipe_runner_test
  termole_pipe_e2e_test
  termole_test_host_api
  termole_t02_host_test
  termole_t03_t05_test
  termole_config_test
  termole_cli_no_console_test
  termole_cmd_test
  termole_wrap_test
  termole_app_window_test
  termole_soak_test
  termole_idle_paint_test
  termole_launcher_test
  termole_lazygit_test
  termole_editor_test
  termole_editor_shell_test
  termole_wsl_test
  termole_fzf_test
  termole_visual_child
  termole_input_child
  termole_visual_test
)
foreach(_t IN LISTS _termole_wmain_targets)
  if(TARGET ${_t})
    termole_unicode_startup(${_t})
  endif()
endforeach()
# Pure-C consumer characterization: links termole statically and verifies the
# defensive ABI behavior plus one real ConPTY session without any C++ runtime.
add_executable(termole_c_abi_characterization_test c_abi_characterization_test.c)
target_link_libraries(termole_c_abi_characterization_test PRIVATE termole termole_build_options)
add_test(NAME c_abi_characterization COMMAND termole_c_abi_characterization_test)
set_tests_properties(c_abi_characterization PROPERTIES
  TIMEOUT 60
  LABELS "characterization;contract"
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

set_tests_properties(
  t01_host_api
  t02_host_args
  t03_t05_session
  t02_cli_args
  t02_cli_help
  t02_cli_error
  t02_cli_cmd
  t02_cli_no_console
  t26_launcher
  t26_cli_pipelines
  t26_cli_parallel
  t17_cmd_target
  t17_wrap
  t01_app_window
  t01_standalone_smoke
  t01_host_smoke
  PROPERTIES LABELS "characterization"
)
set_tests_properties(t01_host_api PROPERTIES LABELS "characterization;contract")
# libghostty-vt adapter smoke: headers + Windows gnu static library under MinGW.
add_executable(termole_ghostty_smoke_test vt_ghostty_smoke_test.c)
target_link_libraries(termole_ghostty_smoke_test PRIVATE ghostty_vt termole_build_options)
add_test(NAME ghostty_vt_smoke COMMAND termole_ghostty_smoke_test)
set_tests_properties(ghostty_vt_smoke PROPERTIES
  TIMEOUT 30
  LABELS "adapter;domain"
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
# C VT adapter contract (include/termole/core.h + src/vt/termole_vt.c).
add_executable(termole_vt_adapter_test
  vt_adapter_test.c
  "${PROJECT_SOURCE_DIR}/src/vt/termole_vt.c"
)
target_include_directories(termole_vt_adapter_test PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(termole_vt_adapter_test PRIVATE ghostty_vt)
add_test(NAME vt_adapter_contract COMMAND termole_vt_adapter_test)
set_tests_properties(vt_adapter_contract PROPERTIES
  TIMEOUT 30
  LABELS "adapter;domain"
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
# Conventions: no Ninja entry points; static product dependencies.
add_test(NAME conventions_no_ninja
  COMMAND "${CMAKE_COMMAND}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckNoNinja.cmake")
set_tests_properties(conventions_no_ninja PROPERTIES LABELS "conventions")

add_test(NAME conventions_static_deps
  COMMAND "${CMAKE_COMMAND}"
    "-DOBJDUMP=${CMAKE_OBJDUMP}"
    "-DEXE=$<TARGET_FILE:termole_app>"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckDeps.cmake")
set_tests_properties(conventions_static_deps PROPERTIES LABELS "conventions")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_test(NAME conventions_release_size
    COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:termole_app>"
      "-DMAX_BYTES=7340032"
      -P "${PROJECT_SOURCE_DIR}/cmake/CheckSize.cmake")
  set_tests_properties(conventions_release_size PROPERTIES LABELS "conventions")
endif()
add_test(NAME conventions_c_core
  COMMAND "${CMAKE_COMMAND}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckBoundary.cmake")
set_tests_properties(conventions_c_core PROPERTIES LABELS "conventions")
add_executable(termole_window_config_test
  window_config_test.cpp
  "${PROJECT_SOURCE_DIR}/apps/common/config.cpp"
)
target_include_directories(termole_window_config_test PRIVATE "${PROJECT_SOURCE_DIR}/apps"
  "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(termole_window_config_test PRIVATE termole_build_options)
add_test(NAME t14_window_config COMMAND termole_window_config_test)
set_tests_properties(t14_window_config PROPERTIES LABELS "characterization")
add_executable(termole_gdi_render_test
  gdi_render_test.c
  "${PROJECT_SOURCE_DIR}/src/gui/gdi_render.c"
  "${PROJECT_SOURCE_DIR}/src/vt/termole_vt.c"
)
target_include_directories(termole_gdi_render_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include"
  "${TERMOLE_GHOSTTY_VT_ROOT}/include")
target_compile_definitions(termole_gdi_render_test PRIVATE GHOSTTY_STATIC)
target_link_libraries(termole_gdi_render_test PRIVATE
  ghostty_vt termole_build_options user32 gdi32 ntdll)
add_test(NAME gdi_render_contract COMMAND termole_gdi_render_test)
set_tests_properties(gdi_render_contract PROPERTIES LABELS "adapter;domain")
add_executable(termole_cview_test cview_test.cpp)
target_include_directories(termole_cview_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(termole_cview_test PRIVATE termole termole_build_options user32 gdi32)
set_target_properties(termole_cview_test PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME cview_contract COMMAND termole_cview_test)
set_tests_properties(cview_contract PROPERTIES LABELS "adapter;domain")
if(TARGET termole_cgui_smoke)
  add_test(NAME cgui_smoke COMMAND termole_cgui_smoke --smoke)
  add_test(NAME cgui_product_smoke COMMAND termole_app --c-gui --smoke)
  add_test(NAME cgui_product_options
    COMMAND termole_app --c-gui --smoke --title=C-GUI-Probe --width=900 --height=600
            --font-size=24 --theme=light)
  set_tests_properties(cgui_product_options PROPERTIES TIMEOUT 20 LABELS "adapter;domain")
  add_test(NAME cgui_product_program
    COMMAND termole_app --c-gui --smoke --program=cmd.exe -- /c echo C-GUI-PROBE)
  set_tests_properties(cgui_product_program PROPERTIES TIMEOUT 20 LABELS "adapter;domain")
  set_tests_properties(cgui_product_smoke PROPERTIES TIMEOUT 20 LABELS "adapter;domain")
  set_tests_properties(cgui_smoke PROPERTIES TIMEOUT 20 LABELS "adapter;domain")
  add_executable(termole_cgui_visual_test cgui_visual_test.cpp)
  termole_unicode_startup(termole_cgui_visual_test)
  target_link_libraries(termole_cgui_visual_test PRIVATE termole_build_options user32 gdi32)
  add_test(NAME cgui_visual COMMAND termole_cgui_visual_test "$<TARGET_FILE:termole_app>")
  set_tests_properties(cgui_visual PROPERTIES TIMEOUT 30 LABELS "visual")
  set(cgui_config "${CMAKE_CURRENT_BINARY_DIR}/cgui-config.toml")
  file(WRITE "${cgui_config}"
    "[window]\nwidth = 700\nheight = 420\ntitle = \"C-GUI-Visual\"\nfont = \"Consolas\"\nfont_size = 20\ntheme = \"light\"\n")
  add_test(NAME cgui_config_visual
    COMMAND termole_cgui_visual_test "$<TARGET_FILE:termole_app>" "${cgui_config}")
  set_tests_properties(cgui_config_visual PROPERTIES TIMEOUT 30 LABELS "visual")
endif()
add_executable(termole_script_stub_test script_stub_test.c)
target_include_directories(termole_script_stub_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(termole_script_stub_test PRIVATE termole termole_build_options)
set_target_properties(termole_script_stub_test PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME script_abi_stub COMMAND termole_script_stub_test)
if(TERMOLE_WITH_QUICKJS)
  target_compile_definitions(termole_script_stub_test PRIVATE TERMOLE_WITH_QUICKJS)
  set_tests_properties(script_abi_stub PROPERTIES TIMEOUT 10)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_test(NAME conventions_quickjs_size
      COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:termole_script_stub_test>"
        "-DMAX_BYTES=8388608"
        -P "${PROJECT_SOURCE_DIR}/cmake/CheckSize.cmake")
    set_tests_properties(conventions_quickjs_size PROPERTIES LABELS "conventions")
  endif()
endif()
set_tests_properties(script_abi_stub PROPERTIES LABELS "contract")
add_executable(termole_windows_pty_test
  windows_pty_test.cpp
)
target_include_directories(termole_windows_pty_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(termole_windows_pty_test PRIVATE termole termole_build_options)
add_test(NAME windows_pty_contract COMMAND termole_windows_pty_test)
set_tests_properties(windows_pty_contract PROPERTIES LABELS "adapter;domain")

add_executable(termole_pty_contract_test pty_contract_test.cpp)
target_include_directories(termole_pty_contract_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(termole_pty_contract_test PRIVATE termole termole_build_options)
add_test(NAME pty_contract_windows COMMAND termole_pty_contract_test)
set_tests_properties(pty_contract_windows PROPERTIES LABELS "adapter;domain" TIMEOUT 15)

add_executable(termole_session_contract_test session_contract_test.cpp)
target_include_directories(termole_session_contract_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(termole_session_contract_test PRIVATE termole termole_build_options)
add_test(NAME session_contract_windows COMMAND termole_session_contract_test)
set_tests_properties(session_contract_windows PROPERTIES LABELS "adapter;domain" TIMEOUT 15)

add_executable(termole_session_ipc_test session_ipc_test.cpp)
target_include_directories(termole_session_ipc_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(termole_session_ipc_test PRIVATE termole termole_build_options)
add_test(NAME session_ipc_windows COMMAND termole_session_ipc_test)
set_tests_properties(session_ipc_windows PROPERTIES LABELS "adapter;contract" TIMEOUT 20)

# These tests instantiate the library's internal C++ result types. Use the
# same Release LTO mode as their implementation in the static archive.
set_target_properties(
  termole_windows_pty_test termole_session_contract_test termole_session_ipc_test
  PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

add_executable(termole_ipc_visual_test windows_ipc_visual_test.cpp)
target_include_directories(termole_ipc_visual_test PRIVATE "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(termole_ipc_visual_test PRIVATE termole_build_options user32 gdi32)
termole_unicode_startup(termole_ipc_visual_test)
add_dependencies(termole_ipc_visual_test termole_app termole_visual_child)
add_test(NAME visual_session_ipc_windows COMMAND termole_ipc_visual_test
  "$<TARGET_FILE:termole_app>" "$<TARGET_FILE:termole_visual_child>")
set_tests_properties(visual_session_ipc_windows PROPERTIES LABELS "visual" TIMEOUT 25)
