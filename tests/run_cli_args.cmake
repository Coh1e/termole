if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT)
  message(FATAL_ERROR "run_cli_args.cmake requires -DAPP=... -DHELPER=... -DROOT=...")
endif()

set(cwd "${ROOT}/cwd with spaces")
set(report "${ROOT}/cli-args-report.txt")
file(MAKE_DIRECTORY "${cwd}")
file(COPY "${HELPER}" DESTINATION "${cwd}")
file(REMOVE "${report}")
set(helper "${cwd}/cli-report-helper.exe")

execute_process(
  COMMAND "${APP}"
    --cwd "${cwd}"
    --env "TERMOLE_PROBE_REPORT=${report}"
    --env "TERMOLE_PROBE_VALUE=hello world"
    --env "TERMOLE_PROBE_EXTRA=中文 env"
    -- "${helper}"
    "alpha"
    "a b"
    "\"quoted\""
    ""
    "中文-🐭"
    "back\\slash\"quote"
    "-dash"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 30
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "CLI exited with '${result}'. stdout:\n${output}\nstderr:\n${error}")
endif()
if(NOT EXISTS "${report}")
  message(FATAL_ERROR "CLI did not produce ${report}. stdout:\n${output}\nstderr:\n${error}")
endif()

file(READ "${report}" text)
file(TO_NATIVE_PATH "${cwd}" native_cwd)

function(expect_line expected)
  string(FIND "${text}" "${expected}\n" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "missing line '${expected}' in report:\n${text}")
  endif()
endfunction()

expect_line("argc=8")
expect_line("arg1=alpha")
expect_line("arg2=a b")
expect_line("arg3=\"quoted\"")
expect_line("arg4len=0")
expect_line("arg4=")
expect_line("arg5=中文-🐭")
set(arg6_line "arg6=back\\slash\"quote")
expect_line("${arg6_line}")
expect_line("arg7=-dash")
set(cwd_line "cwd=${native_cwd}")
expect_line("${cwd_line}")
expect_line("env.TERMOLE_PROBE_VALUE=hello world")
expect_line("env.TERMOLE_PROBE_EXTRA=中文 env")
expect_line("stdout.console=1")
expect_line("stdin.console=1")
expect_line("stderr.console=1")

string(FIND "${text}" "env.SystemRoot=\n" empty_system_root)
if(NOT empty_system_root EQUAL -1)
  message(FATAL_ERROR "SystemRoot was not inherited:\n${text}")
endif()

message(STATUS "CLI exact argv/cwd/environment passed")
