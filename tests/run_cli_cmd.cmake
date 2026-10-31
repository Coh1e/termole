if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT)
  message(FATAL_ERROR "run_cli_cmd.cmake requires -DAPP=... -DHELPER=... -DROOT=...")
endif()

set(cwd "${ROOT}/cmd cwd")
set(report "${ROOT}/cli-cmd-report.txt")
set(script "${ROOT}/run-cli.cmd")
file(MAKE_DIRECTORY "${cwd}")
file(COPY "${HELPER}" DESTINATION "${cwd}")
file(REMOVE "${report}")
set(helper "${cwd}/cli-report-helper.exe")

file(WRITE "${script}"
  "@echo off\r\n"
  "\"${APP}\" --cwd \"${cwd}\" --env \"TERMOLE_PROBE_REPORT=${report}\" --env \"TERMOLE_PROBE_VALUE=cmd value\" -e \"${helper}\" \"alpha\" \"a b\"\r\n"
)

execute_process(COMMAND cmd.exe /d /c "${script}" RESULT_VARIABLE result
                OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "CMD launch exited with '${result}'. stdout:\n${output}\nstderr:\n${error}")
endif()
if(NOT EXISTS "${report}")
  message(FATAL_ERROR "CMD launch did not produce ${report}. stdout:\n${output}\nstderr:\n${error}")
endif()

file(READ "${report}" text)
file(TO_NATIVE_PATH "${cwd}" native_cwd)
function(expect_line expected)
  string(FIND "${text}" "${expected}\n" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "missing line '${expected}' in CMD report:\n${text}")
  endif()
endfunction()

expect_line("argc=3")
expect_line("arg1=alpha")
expect_line("arg2=a b")
set(cwd_line "cwd=${native_cwd}")
expect_line("${cwd_line}")
expect_line("env.TERMOLE_PROBE_VALUE=cmd value")
expect_line("stdout.console=1")
message(STATUS "CMD launch/wait/environment passed")
