# Verifies that the installed executable does not import MinGW runtime DLLs.
if(NOT DEFINED EXE OR NOT EXISTS "${EXE}")
  message(FATAL_ERROR "CheckDeps: EXE is missing: ${EXE}")
endif()
if(NOT DEFINED OBJDUMP OR OBJDUMP STREQUAL "")
  message(FATAL_ERROR "CheckDeps: OBJDUMP is not configured")
endif()

execute_process(
  COMMAND "${OBJDUMP}" -p "${EXE}"
  OUTPUT_VARIABLE output
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "CheckDeps: objdump failed for ${EXE}")
endif()

string(REGEX MATCHALL "DLL Name: [^\r\n]+" imports "${output}")
set(forbidden "libstdc|libgcc|libwinpthread|termole\\.dll")
set(clean TRUE)

foreach(line IN LISTS imports)
  string(REGEX REPLACE "^DLL Name: " "" name "${line}")
  string(STRIP "${name}" name)
  string(TOLOWER "${name}" lower)
  if(lower MATCHES "${forbidden}")
    message(SEND_ERROR "forbidden runtime import: ${name}")
    set(clean FALSE)
  endif()
endforeach()

if(NOT clean)
  message(FATAL_ERROR "CheckDeps: ${EXE} is not self-contained")
endif()

message(STATUS "CheckDeps: ${EXE} has no MinGW or Termole DLL imports")
