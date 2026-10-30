if(NOT DEFINED APP OR NOT DEFINED RESULT)
  message(FATAL_ERROR "run_smoke.cmake requires -DAPP=<exe> and -DRESULT=<file>")
endif()

file(REMOVE "${RESULT}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "TERMOLE_SMOKE_RESULT=${RESULT}" "${APP}"
  RESULT_VARIABLE result
  TIMEOUT 45
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "smoke entry point exited with '${result}'")
endif()
if(NOT EXISTS "${RESULT}")
  message(FATAL_ERROR "smoke entry point did not write ${RESULT}")
endif()

file(READ "${RESULT}" content)
string(FIND "${content}" "termole-smoke-ok" marker)
if(marker EQUAL -1)
  message(FATAL_ERROR "screen smoke marker missing. Captured:\n${content}")
endif()

message(STATUS "smoke screen contains termole-smoke-ok")
