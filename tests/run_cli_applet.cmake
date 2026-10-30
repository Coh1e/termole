if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT)
  message(FATAL_ERROR "run_cli_applet.cmake requires -DAPP=... -DHELPER=... -DROOT=...")
endif()
set(report "${ROOT}/applet.txt")
file(MAKE_DIRECTORY "${ROOT}")
file(REMOVE "${report}")

execute_process(
  COMMAND "${APP}" --columns 40 --rows 12 --center --opacity 80 --borderless
    --title "Termole Applet" --env "TERMOLE_PROBE_REPORT=${report}" --
    "${HELPER}" --exit-code 0
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "applet launch returned '${result}'. stderr:\n${error}")
endif()
if(NOT EXISTS "${report}")
  message(FATAL_ERROR "applet target did not run")
endif()
file(READ "${report}" text)
string(FIND "${text}" "console.cols=40\n" cols)
string(FIND "${text}" "console.rows=12\n" rows)
if(cols EQUAL -1 OR rows EQUAL -1)
  message(FATAL_ERROR "applet grid was not applied:\n${text}")
endif()

foreach(arguments
    "--columns 0"
    "--rows 1001"
    "--opacity 5"
    "--icon ${ROOT}/missing.ico"
    "--center --x 10")
  separate_arguments(parts WINDOWS_COMMAND "${arguments}")
  execute_process(COMMAND "${APP}" ${parts} RESULT_VARIABLE invalid_result
                  OUTPUT_VARIABLE invalid_output ERROR_VARIABLE invalid_error TIMEOUT 10)
  if(NOT invalid_result EQUAL 2)
    message(FATAL_ERROR "invalid applet option '${arguments}' returned ${invalid_result}:\n${invalid_error}")
  endif()
endforeach()
message(STATUS "applet grid and invalid options passed")
