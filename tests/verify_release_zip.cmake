cmake_minimum_required(VERSION 3.28)

foreach(input IN ITEMS Archive Evidence)
  if(NOT DEFINED ${input} OR "${${input}}" STREQUAL "")
    message(FATAL_ERROR "Missing required ${input} argument")
  endif()
endforeach()
file(READ "${Archive}.sha256" checksum)
file(SHA256 "${Archive}" digest)
get_filename_component(filename "${Archive}" NAME)
string(STRIP "${checksum}" checksum)
if(NOT checksum STREQUAL "${digest}  ${filename}")
  message(FATAL_ERROR "Checksum does not match ${filename}")
endif()

get_filename_component(stem "${Archive}" NAME_WLE)
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef run)
set(record "${Evidence}/${digest}/${run}")
if(EXISTS "${record}")
  message(FATAL_ERROR "Evidence directory already exists: ${record}")
endif()
set(root "${record}/便携 验收")
file(MAKE_DIRECTORY "${root}")
file(ARCHIVE_EXTRACT INPUT "${Archive}" DESTINATION "${root}")
set(package "${root}/${stem}")
file(READ "${package}/share/termole/source-revision.txt" revision)
string(STRIP "${revision}" revision)
if(NOT DEFINED CandidateSource)
  get_filename_component(CandidateSource "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(NOT EXISTS "${CandidateSource}/.git")
  message(FATAL_ERROR "Release acceptance requires the candidate's Git checkout.")
endif()
find_program(git NAMES git REQUIRED)
execute_process(COMMAND "${git}" -C "${CandidateSource}" rev-parse --verify HEAD
  OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${git}" -C "${CandidateSource}" status --porcelain --untracked-files=normal
  OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT changes STREQUAL "" OR NOT head STREQUAL revision)
  message(FATAL_ERROR "Use a clean checkout matching the packaged revision '${revision}'. Current HEAD: ${head}")
endif()

foreach(tool IN ITEMS PowerShell Fzf Capture)
  if(NOT EXISTS "${${tool}}")
    message(FATAL_ERROR "Missing ${tool} executable for the desktop preview check: ${${tool}}")
  endif()
endforeach()
set(log "${record}/desktop-preview.log")
# Windows PowerShell 5.1 must discover its own modules when called from pwsh.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=PSModulePath --
    "${PowerShell}" -NoProfile -ExecutionPolicy Bypass -STA
    -File "${CMAKE_CURRENT_LIST_DIR}/release/desktop-preview.ps1" -Package "${package}"
    -Evidence "${record}/desktop-preview" -Fzf "${Fzf}" -Capture "${Capture}"
  WORKING_DIRECTORY "${package}" RESULT_VARIABLE result OUTPUT_FILE "${log}" ERROR_FILE "${log}" TIMEOUT 300)
if(NOT result STREQUAL "0")
  file(READ "${log}" output)
  message(FATAL_ERROR "Desktop preview failed (${result}). Evidence: ${record}\n${output}")
endif()
file(WRITE "${record}/acceptance.txt"
  "${filename}\nSHA-256: ${digest}\nSource: ${revision}\nPackage: ${package}\n"
  "Passed: archive checksum, clean source revision, DXGI first preview frame\n")
message(STATUS "Verified ${filename} SHA-256 ${digest} from ${revision}. Evidence: ${record}")
