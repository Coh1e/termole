if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT OR NOT DEFINED POWERSHELL)
  message(FATAL_ERROR "run_cli_ps.cmake requires -DAPP=... -DHELPER=... -DROOT=... -DPOWERSHELL=...")
endif()

set(cwd "${ROOT}/ps cwd")
set(report "${ROOT}/cli-ps-report.txt")
file(MAKE_DIRECTORY "${cwd}")
file(COPY "${HELPER}" DESTINATION "${cwd}")
file(REMOVE "${report}")
set(helper "${cwd}/cli-report-helper.exe")

execute_process(
  COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_LIST_DIR}/run_cli_ps.ps1"
    -Exe "${APP}" -Helper "${helper}" -Cwd "${cwd}" -Report "${report}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 30
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "PowerShell launch exited with '${result}'. stdout:\n${output}\nstderr:\n${error}")
endif()
if(NOT EXISTS "${report}")
  message(FATAL_ERROR "PowerShell launch did not produce ${report}. stdout:\n${output}\nstderr:\n${error}")
endif()

file(READ "${report}" text)
file(TO_NATIVE_PATH "${cwd}" native_cwd)
function(expect_line expected)
  string(FIND "${text}" "${expected}\n" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "missing line '${expected}' in PowerShell report:\n${text}")
  endif()
endfunction()

expect_line("argc=3")
expect_line("arg1=alpha")
expect_line("arg2=a b")
set(cwd_line "cwd=${native_cwd}")
expect_line("${cwd_line}")
expect_line("env.TERMOLE_PROBE_VALUE=ps value")
expect_line("stdout.console=1")
message(STATUS "PowerShell launch/wait/environment passed")
