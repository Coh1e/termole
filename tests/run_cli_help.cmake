if(NOT DEFINED APP)
  message(FATAL_ERROR "run_cli_help.cmake requires -DAPP=...")
endif()

execute_process(COMMAND "${APP}" --help RESULT_VARIABLE result OUTPUT_VARIABLE output
                ERROR_VARIABLE error TIMEOUT 10)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "--help exited with '${result}'. stderr:\n${error}")
endif()
string(FIND "${output}" "Usage:" usage)
if(usage EQUAL -1)
  message(FATAL_ERROR "--help did not print usage. stdout:\n${output}")
endif()

execute_process(COMMAND "${APP}" --version RESULT_VARIABLE version_result
                OUTPUT_VARIABLE version_output ERROR_VARIABLE version_error TIMEOUT 10)
if(NOT version_result EQUAL 0)
  message(FATAL_ERROR "--version exited with '${version_result}'. stderr:\n${version_error}")
endif()
string(FIND "${version_output}" "Termole " version_marker)
if(version_marker EQUAL -1)
  message(FATAL_ERROR "--version did not print a version. stdout:\n${version_output}")
endif()
message(STATUS "CLI help/version passed")
