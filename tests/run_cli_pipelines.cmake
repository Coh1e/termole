if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT)
  message(FATAL_ERROR "run_cli_pipelines.cmake requires -DAPP=... -DHELPER=... -DROOT=...")
endif()
file(MAKE_DIRECTORY "${ROOT}")

file(TO_NATIVE_PATH "${APP}" app_native)
file(TO_NATIVE_PATH "${HELPER}" helper_native)
file(TO_NATIVE_PATH "${ROOT}" root_native)
set(input "${ROOT}/pipe-input.txt")
set(output "${ROOT}/pipe-output.txt")
set(script "${ROOT}/pipe.cmd")
file(TO_NATIVE_PATH "${input}" input_native)
file(TO_NATIVE_PATH "${output}" output_native)
file(WRITE "${input}" "alpha\r\nbeta\r\n中文-🐭\r\n")

# Real CMD pipeline: the caller's pipe ends up as the child's stdin/stdout and
# nothing else may enter the result.
file(WRITE "${script}"
  "@echo off\r\n"
  "type \"${input_native}\" | \"${app_native}\" --stdio \"${helper_native}\" --copy-stdin > \"${output_native}\"\r\n"
  "exit /b %errorlevel%\r\n"
)
execute_process(COMMAND cmd.exe /d /c "${script}" RESULT_VARIABLE result
                OUTPUT_VARIABLE command_output ERROR_VARIABLE command_error TIMEOUT 30)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "CMD --stdio pipeline exited with '${result}'. stdout:\n${command_output}\nstderr:\n${command_error}")
endif()
file(READ "${input}" expected)
file(READ "${output}" actual)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "CMD --stdio pipeline changed the bytes. expected:\n${expected}\nactual:\n${actual}\nstderr:\n${command_error}")
endif()

# A larger CMD pipeline exercises backpressure: the input is bigger than the
# pipe buffer, so both sides must stream rather than buffer.
set(big_input "${ROOT}/big-input.txt")
set(big_output "${ROOT}/big-output.txt")
set(big_text "")
foreach(index RANGE 1 8000)
  string(APPEND big_text "line-${index} payload\r\n")
endforeach()
file(WRITE "${big_input}" "${big_text}")
file(TO_NATIVE_PATH "${big_input}" big_input_native)
file(TO_NATIVE_PATH "${big_output}" big_output_native)
set(big_script "${ROOT}/pipe-big.cmd")
file(WRITE "${big_script}"
  "@echo off\r\n"
  "type \"${big_input_native}\" | \"${app_native}\" --stdio \"${helper_native}\" --copy-stdin > \"${big_output_native}\"\r\n"
  "exit /b %errorlevel%\r\n"
)
execute_process(COMMAND cmd.exe /d /c "${big_script}" RESULT_VARIABLE big_result
                OUTPUT_VARIABLE big_command_output ERROR_VARIABLE big_command_error TIMEOUT 60)
if(NOT big_result EQUAL 0)
  message(FATAL_ERROR "CMD large --stdio pipeline exited with '${big_result}'. stderr:\n${big_command_error}")
endif()
file(READ "${big_input}" big_expected)
file(READ "${big_output}" big_actual)
if(NOT big_actual STREQUAL big_expected)
  message(FATAL_ERROR "CMD large --stdio pipeline changed the bytes")
endif()

# The caller closes the result pipe early: the pipeline must end instead of
# blocking on the remaining output.
set(early_script "${ROOT}/pipe-early-close.cmd")
file(WRITE "${early_script}"
  "@echo off\r\n"
  "type \"${big_input_native}\" | \"${app_native}\" --stdio \"${helper_native}\" --copy-stdin | \"${helper_native}\" --read-one > NUL\r\n"
  "exit /b %errorlevel%\r\n"
)
execute_process(COMMAND cmd.exe /d /c "${early_script}" RESULT_VARIABLE early_result
                OUTPUT_VARIABLE early_output ERROR_VARIABLE early_error TIMEOUT 30)
if(NOT early_result EQUAL 0)
  message(FATAL_ERROR "CMD early-closed --stdio pipeline did not finish cleanly (${early_result}). stderr:\n${early_error}")
endif()

# CMD waits for the launcher, so a following command sees the program's status.
set(exit_script "${ROOT}/pipe-exit.cmd")
file(WRITE "${exit_script}"
  "@echo off\r\n"
  "type \"${input_native}\" | \"${app_native}\" --stdio \"${helper_native}\" --copy-stdin --exit-code 6 > \"${output_native}\"\r\n"
  "exit /b %errorlevel%\r\n"
)
execute_process(COMMAND cmd.exe /d /c "${exit_script}" RESULT_VARIABLE exit_result
                OUTPUT_VARIABLE exit_output ERROR_VARIABLE exit_error TIMEOUT 30)
if(NOT exit_result EQUAL 6)
  message(FATAL_ERROR "CMD pipeline returned '${exit_result}', expected the program's 6. stderr:\n${exit_error}")
endif()

if(DEFINED POWERSHELL AND EXISTS "${POWERSHELL}")
  execute_process(
    COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_LIST_DIR}/run_cli_pipeline.ps1"
      -Exe "${app_native}" -Helper "${helper_native}"
    RESULT_VARIABLE powershell_result
    OUTPUT_VARIABLE powershell_output
    ERROR_VARIABLE powershell_error
    TIMEOUT 40
  )
  if(NOT powershell_result EQUAL 0)
    message(FATAL_ERROR "PowerShell --stdio pipeline failed (${powershell_result}). stdout:\n${powershell_output}\nstderr:\n${powershell_error}")
  endif()
else()
  message(STATUS "powershell.exe not found: PowerShell pipeline check is omitted.")
endif()

message(STATUS "CMD/PowerShell --stdio pipelines and exit-status waiting passed")
