if(NOT DEFINED APP OR NOT DEFINED ROOT)
  message(FATAL_ERROR "run_cli_error.cmake requires -DAPP=... -DROOT=...")
endif()
file(MAKE_DIRECTORY "${ROOT}")

execute_process(COMMAND "${APP}" --definitely-not-an-option RESULT_VARIABLE result
                OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(NOT result EQUAL 2)
  message(FATAL_ERROR "invalid option exited with '${result}', expected 2. stdout:\n${output}\nstderr:\n${error}")
endif()
string(FIND "${error}" "unknown option" diagnostic)
if(diagnostic EQUAL -1)
  message(FATAL_ERROR "invalid option did not print a diagnostic. stderr:\n${error}")
endif()

execute_process(COMMAND "${APP}" --env NO_EQUALS RESULT_VARIABLE env_result
                OUTPUT_VARIABLE env_output ERROR_VARIABLE env_error TIMEOUT 10)
if(NOT env_result EQUAL 2)
  message(FATAL_ERROR "malformed --env exited with '${env_result}', expected 2. stderr:\n${env_error}")
endif()
string(FIND "${env_error}" "invalid --env" env_diagnostic)
if(env_diagnostic EQUAL -1)
  message(FATAL_ERROR "malformed --env did not print a diagnostic. stderr:\n${env_error}")
endif()

execute_process(COMMAND "${APP}" --font-size 7 RESULT_VARIABLE font_result
                OUTPUT_VARIABLE font_output ERROR_VARIABLE font_error TIMEOUT 10)
if(NOT font_result EQUAL 2)
  message(FATAL_ERROR "invalid --font-size exited with '${font_result}', expected 2. stderr:\n${font_error}")
endif()
execute_process(COMMAND "${APP}" --theme neon RESULT_VARIABLE theme_result
                OUTPUT_VARIABLE theme_output ERROR_VARIABLE theme_error TIMEOUT 10)
if(NOT theme_result EQUAL 2)
  message(FATAL_ERROR "invalid --theme exited with '${theme_result}', expected 2. stderr:\n${theme_error}")
endif()

foreach(bad_invocation "-e" "--stdio")
  separate_arguments(parts WINDOWS_COMMAND "${bad_invocation}")
  execute_process(COMMAND "${APP}" ${parts} RESULT_VARIABLE mode_result
                  OUTPUT_VARIABLE mode_output ERROR_VARIABLE mode_error TIMEOUT 10)
  if(NOT mode_result EQUAL 2)
    message(FATAL_ERROR "'${bad_invocation}' exited with '${mode_result}', expected 2. stderr:\n${mode_error}")
  endif()
endforeach()

execute_process(COMMAND "${APP}" --cwd "${ROOT}/missing-cwd" -e "cmd.exe" /c exit
                RESULT_VARIABLE cwd_result OUTPUT_VARIABLE cwd_output
                ERROR_VARIABLE cwd_error TIMEOUT 20)
if(NOT cwd_result EQUAL 1)
  message(FATAL_ERROR "bad --cwd exited with '${cwd_result}', expected 1. stderr:\n${cwd_error}")
endif()
string(FIND "${cwd_error}" "working directory" cwd_diagnostic)
if(cwd_diagnostic EQUAL -1)
  message(FATAL_ERROR "bad --cwd did not print a diagnostic. stderr:\n${cwd_error}")
endif()

execute_process(COMMAND "${APP}" -e "termole-no-such-program-12345.exe" RESULT_VARIABLE missing_result
                OUTPUT_VARIABLE missing_output ERROR_VARIABLE missing_error TIMEOUT 20)
if(NOT missing_result EQUAL 1)
  message(FATAL_ERROR "missing program exited with '${missing_result}', expected 1. stdout:\n${missing_output}\nstderr:\n${missing_error}")
endif()
string(FIND "${missing_error}" "not found" missing_diagnostic)
if(missing_diagnostic EQUAL -1)
  message(FATAL_ERROR "missing program did not print a diagnostic. stderr:\n${missing_error}")
endif()
message(STATUS "CLI invalid-option/env/cwd/missing-program diagnostics passed")
