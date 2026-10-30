if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT OR NOT DEFINED POWERSHELL)
  message(FATAL_ERROR "run_cli_exit.cmake requires -DAPP=... -DHELPER=... -DROOT=... -DPOWERSHELL=...")
endif()
set(report "${ROOT}/exit-close.txt")
file(MAKE_DIRECTORY "${ROOT}")
file(REMOVE "${report}")

execute_process(
  COMMAND "${APP}" --env "TERMOLE_PROBE_REPORT=${report}" --close-on-exit -- "${HELPER}" --exit-code 7
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20
)
if(NOT result EQUAL 7)
  message(FATAL_ERROR "--close-on-exit returned '${result}', expected 7. stderr:\n${error}")
endif()
if(NOT EXISTS "${report}")
  message(FATAL_ERROR "--close-on-exit did not run the child")
endif()

set(report_keep "${ROOT}/exit-keep.txt")
file(REMOVE "${report_keep}")
execute_process(
  COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_LIST_DIR}/run_cli_keep.ps1"
    -Exe "${APP}" -Helper "${HELPER}" -Report "${report_keep}"
  RESULT_VARIABLE keep_result OUTPUT_VARIABLE keep_output ERROR_VARIABLE keep_error TIMEOUT 30
)
if(NOT keep_result EQUAL 0)
  message(FATAL_ERROR "default keep-window test failed (${keep_result}). stdout:\n${keep_output}\nstderr:\n${keep_error}")
endif()
if(NOT EXISTS "${report_keep}")
  message(FATAL_ERROR "keep-window target did not run")
endif()
message(STATUS "CLI exit/keep-window policy passed")
