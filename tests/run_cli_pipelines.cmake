if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT)
  message(FATAL_ERROR "run_cli_pipelines.cmake requires -DAPP=... -DHELPER=... -DROOT=...")
endif()
file(MAKE_DIRECTORY "${ROOT}")

file(TO_NATIVE_PATH "${APP}" app_native)
file(TO_NATIVE_PATH "${HELPER}" helper_native)
file(TO_NATIVE_PATH "${ROOT}" root_native)
set(input "${ROOT}/pipe-input.txt")
set(output "${ROOT}/pipe-output.txt")
file(TO_NATIVE_PATH "${input}" input_native)
file(TO_NATIVE_PATH "${output}" output_native)
file(WRITE "${input}" "alpha\r\nbeta\r\n中文-🐭\r\n")

# Real CMD pipeline: the caller's pipe ends up as the child's stdin/stdout and
# nothing else may enter the result.
function(assert_same_bytes label expected_path actual_path)
  file(READ "${expected_path}" expected)
  file(READ "${actual_path}" actual)
  if(NOT actual STREQUAL expected)
    file(SIZE "${expected_path}" expected_size)
    file(SIZE "${actual_path}" actual_size)
    string(SHA256 expected_hash "${expected}")
    string(SHA256 actual_hash "${actual}")
    message(FATAL_ERROR "${label} changed the bytes (expected ${expected_size} B sha256 ${expected_hash}, actual ${actual_size} B sha256 ${actual_hash})")
  endif()
endfunction()

function(run_cmd_copy_pipeline label input_path output_path timeout)
  set(script "${ROOT}/pipe-${label}.cmd")
  file(TO_NATIVE_PATH "${input_path}" input_path_native)
  file(TO_NATIVE_PATH "${output_path}" output_path_native)
  file(WRITE "${script}"
    "@echo off\r\n"
    "type \"${input_path_native}\" | \"${app_native}\" --stdio \"${helper_native}\" --copy-stdin > \"${output_path_native}\"\r\n"
    "exit /b %errorlevel%\r\n"
  )
  execute_process(COMMAND cmd.exe /d /c "${script}" RESULT_VARIABLE result
                  OUTPUT_VARIABLE command_output ERROR_VARIABLE command_error TIMEOUT ${timeout})
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label} --stdio pipeline exited with '${result}'. stdout:\n${command_output}\nstderr:\n${command_error}")
  endif()
  assert_same_bytes("${label} --stdio pipeline" "${input_path}" "${output_path}")
endfunction()

run_cmd_copy_pipeline("small" "${input}" "${output}" 30)

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
run_cmd_copy_pipeline("big" "${big_input}" "${big_output}" 60)

# T16: multi-megabyte input through a real CMD pipe. The payload is larger than
# any pipe buffer, so the whole path must stream under backpressure.
set(huge_input "${ROOT}/huge-input.txt")
set(huge_output "${ROOT}/huge-output.txt")
string(REPEAT "0123456789abcdef0123456789abcdef\n" 65536 huge_text)
file(WRITE "${huge_input}" "${huge_text}")
run_cmd_copy_pipeline("huge" "${huge_input}" "${huge_output}" 120)

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

# T16: fzf is only an external example. The same candidate file is filtered
# directly and through termole --stdio; stdout must match byte for byte and the
# program status must be forwarded. --multi is just an argument to forward.
if(DEFINED FZF AND EXISTS "${FZF}")
  file(TO_NATIVE_PATH "${FZF}" fzf_native)
  set(fzf_candidates "${ROOT}/fzf-candidates.txt")
  set(fzf_direct "${ROOT}/fzf-direct.txt")
  set(fzf_via "${ROOT}/fzf-via.txt")
  set(fzf_multi_direct "${ROOT}/fzf-multi-direct.txt")
  set(fzf_multi_via "${ROOT}/fzf-multi-via.txt")
  set(fzf_nomatch "${ROOT}/fzf-nomatch.txt")
  file(TO_NATIVE_PATH "${fzf_candidates}" fzf_candidates_native)
  file(TO_NATIVE_PATH "${fzf_direct}" fzf_direct_native)
  file(TO_NATIVE_PATH "${fzf_via}" fzf_via_native)
  file(TO_NATIVE_PATH "${fzf_multi_direct}" fzf_multi_direct_native)
  file(TO_NATIVE_PATH "${fzf_multi_via}" fzf_multi_via_native)
  file(TO_NATIVE_PATH "${fzf_nomatch}" fzf_nomatch_native)
  file(WRITE "${fzf_candidates}" "alpha\nbeta\n中文-🐭\nmulti word\nzeta\n")

  file(WRITE "${ROOT}/fzf-direct.cmd"
    "@echo off\r\n"
    "\"${fzf_native}\" --filter=alp < \"${fzf_candidates_native}\" > \"${fzf_direct_native}\"\r\n"
    "exit /b %errorlevel%\r\n"
  )
  execute_process(COMMAND cmd.exe /d /c "${ROOT}/fzf-direct.cmd"
                  RESULT_VARIABLE fzf_direct_result ERROR_VARIABLE fzf_direct_error TIMEOUT 30)
  if(NOT fzf_direct_result EQUAL 0)
    message(FATAL_ERROR "direct fzf --filter exited with '${fzf_direct_result}'. stderr:\n${fzf_direct_error}")
  endif()

  file(WRITE "${ROOT}/fzf-via.cmd"
    "@echo off\r\n"
    "type \"${fzf_candidates_native}\" | \"${app_native}\" --stdio \"${fzf_native}\" --filter=alp > \"${fzf_via_native}\"\r\n"
    "exit /b %errorlevel%\r\n"
  )
  execute_process(COMMAND cmd.exe /d /c "${ROOT}/fzf-via.cmd"
                  RESULT_VARIABLE fzf_via_result ERROR_VARIABLE fzf_via_error TIMEOUT 30)
  if(NOT fzf_via_result EQUAL 0)
    message(FATAL_ERROR "termole --stdio fzf --filter exited with '${fzf_via_result}'. stderr:\n${fzf_via_error}")
  endif()
  assert_same_bytes("termole --stdio fzf --filter" "${fzf_direct}" "${fzf_via}")
  file(READ "${fzf_via}" fzf_via_text)
  string(ASCII 27 fzf_escape)
  string(FIND "${fzf_via_text}" "${fzf_escape}" fzf_escape_at)
  if(NOT fzf_escape_at EQUAL -1)
    message(FATAL_ERROR "termole --stdio injected terminal drawing into the fzf result")
  endif()

  file(WRITE "${ROOT}/fzf-multi-direct.cmd"
    "@echo off\r\n"
    "\"${fzf_native}\" --filter=a --multi < \"${fzf_candidates_native}\" > \"${fzf_multi_direct_native}\"\r\n"
    "exit /b %errorlevel%\r\n"
  )
  execute_process(COMMAND cmd.exe /d /c "${ROOT}/fzf-multi-direct.cmd"
                  RESULT_VARIABLE fzf_multi_direct_result TIMEOUT 30)
  file(WRITE "${ROOT}/fzf-multi-via.cmd"
    "@echo off\r\n"
    "type \"${fzf_candidates_native}\" | \"${app_native}\" --stdio \"${fzf_native}\" --filter=a --multi > \"${fzf_multi_via_native}\"\r\n"
    "exit /b %errorlevel%\r\n"
  )
  execute_process(COMMAND cmd.exe /d /c "${ROOT}/fzf-multi-via.cmd"
                  RESULT_VARIABLE fzf_multi_via_result TIMEOUT 30)
  if(NOT fzf_multi_direct_result EQUAL 0 OR NOT fzf_multi_via_result EQUAL 0)
    message(FATAL_ERROR "fzf --multi exited with direct='${fzf_multi_direct_result}' via='${fzf_multi_via_result}'")
  endif()
  assert_same_bytes("termole --stdio fzf --multi" "${fzf_multi_direct}" "${fzf_multi_via}")

  file(WRITE "${ROOT}/fzf-nomatch.cmd"
    "@echo off\r\n"
    "type \"${fzf_candidates_native}\" | \"${app_native}\" --stdio \"${fzf_native}\" --filter=zzz > \"${fzf_nomatch_native}\"\r\n"
    "exit /b %errorlevel%\r\n"
  )
  execute_process(COMMAND cmd.exe /d /c "${ROOT}/fzf-nomatch.cmd"
                  RESULT_VARIABLE fzf_nomatch_result TIMEOUT 30)
  if(NOT fzf_nomatch_result EQUAL 1)
    message(FATAL_ERROR "termole --stdio did not forward fzf's no-match status 1 (got '${fzf_nomatch_result}')")
  endif()
else()
  message(STATUS "fzf.exe not found: the --stdio fzf filter check is omitted.")
endif()

if(DEFINED POWERSHELL AND EXISTS "${POWERSHELL}")
  execute_process(
    COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_LIST_DIR}/run_cli_pipeline.ps1"
      -Exe "${app_native}" -Helper "${helper_native}" -Fzf "${FZF}"
    RESULT_VARIABLE powershell_result
    OUTPUT_VARIABLE powershell_output
    ERROR_VARIABLE powershell_error
    TIMEOUT 90
  )
  if(NOT powershell_result EQUAL 0)
    message(FATAL_ERROR "PowerShell --stdio pipeline failed (${powershell_result}). stdout:\n${powershell_output}\nstderr:\n${powershell_error}")
  endif()
else()
  message(STATUS "powershell.exe not found: PowerShell pipeline check is omitted.")
endif()

if(DEFINED FZF AND EXISTS "${FZF}")
  set(fzf_status "fzf filter verified")
else()
  set(fzf_status "fzf filter SKIPPED")
endif()
message(STATUS "CMD/PowerShell --stdio pipelines and exit-status waiting passed (${fzf_status})")
