if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT)
  message(FATAL_ERROR "run_cli_parallel.cmake requires -DAPP=... -DHELPER=... -DROOT=...")
endif()

set(cwd_a "${ROOT}/parallel A")
set(cwd_b "${ROOT}/parallel B")
set(report_a "${ROOT}/parallel-a.txt")
set(report_b "${ROOT}/parallel-b.txt")
file(MAKE_DIRECTORY "${cwd_a}" "${cwd_b}")
file(COPY "${HELPER}" DESTINATION "${cwd_a}")
file(COPY "${HELPER}" DESTINATION "${cwd_b}")
file(REMOVE "${report_a}" "${report_b}")
set(helper_a "${cwd_a}/cli-report-helper.exe")
set(helper_b "${cwd_b}/cli-report-helper.exe")

# Multiple COMMAND clauses launch both instances concurrently.
execute_process(
  COMMAND "${APP}" --cwd "${cwd_a}" --env "TERMOLE_PROBE_REPORT=${report_a}"
    --env "TERMOLE_PROBE_VALUE=parallel A" -e "${helper_a}" "parallel A" "a b"
  COMMAND "${APP}" --cwd "${cwd_b}" --env "TERMOLE_PROBE_REPORT=${report_b}"
    --env "TERMOLE_PROBE_VALUE=parallel B" -e "${helper_b}" "parallel B" "a b"
  RESULTS_VARIABLE results
  TIMEOUT 30
)
if(NOT results STREQUAL "0;0")
  message(FATAL_ERROR "Parallel CLI exit codes: ${results}")
endif()
if(NOT EXISTS "${report_a}" OR NOT EXISTS "${report_b}")
  message(FATAL_ERROR "parallel CLI launch did not write both reports")
endif()

file(READ "${report_a}" text_a)
file(READ "${report_b}" text_b)
file(TO_NATIVE_PATH "${cwd_a}" native_cwd_a)
file(TO_NATIVE_PATH "${cwd_b}" native_cwd_b)
function(expect_in text expected where)
  string(FIND "${text}" "${expected}\n" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "missing line '${expected}' in ${where}:\n${text}")
  endif()
endfunction()

expect_in("${text_a}" "arg1=parallel A" "instance A report")
expect_in("${text_a}" "arg2=a b" "instance A report")
expect_in("${text_a}" "env.TERMOLE_PROBE_VALUE=parallel A" "instance A report")
expect_in("${text_a}" "cwd=${native_cwd_a}" "instance A report")
expect_in("${text_b}" "arg1=parallel B" "instance B report")
expect_in("${text_b}" "arg2=a b" "instance B report")
expect_in("${text_b}" "env.TERMOLE_PROBE_VALUE=parallel B" "instance B report")
expect_in("${text_b}" "cwd=${native_cwd_b}" "instance B report")
message(STATUS "independent CLI instances passed")
