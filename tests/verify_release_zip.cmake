cmake_minimum_required(VERSION 3.28)

# Read the checksum emitted by CPack before extracting the candidate.
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
set(root "${record}/便携 验收")
file(MAKE_DIRECTORY "${root}")
file(ARCHIVE_EXTRACT INPUT "${Archive}" DESTINATION "${root}")
set(package "${root}/${stem}")
set(app "${package}/bin/termole.exe")
foreach(path IN ITEMS bin/termole.exe LICENSE THIRD_PARTY_NOTICES.md licenses/quickjs.txt
    licenses/libghostty-vt.txt docs/architecture.md share/termole/examples/select-file.ps1
    share/termole/examples/preview-file.ps1 share/termole/examples/markdown-quote.js
    share/termole/examples/markdown-quote.toml include/termole/termole.h
    lib/termole.a lib/ghostty-vt-static.lib lib/cmake/termole/termoleConfig.cmake
    share/termole/source-revision.txt share/termole/examples/sdk-host/CMakeLists.txt)
  if(NOT EXISTS "${package}/${path}")
    message(FATAL_ERROR "${filename} lacks ${path}")
  endif()
endforeach()
file(READ "${package}/share/termole/source-revision.txt" revision)
string(STRIP "${revision}" revision)
get_filename_component(source "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT EXISTS "${source}/.git")
  message(FATAL_ERROR "Release acceptance requires the candidate's Git checkout.")
endif()
find_program(git NAMES git REQUIRED)
execute_process(COMMAND "${git}" -C "${source}" rev-parse --verify HEAD
  OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${git}" -C "${source}" status --porcelain
  OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(changes OR NOT head STREQUAL revision)
  message(FATAL_ERROR "Use a clean checkout matching the packaged revision '${revision}'. Current HEAD: ${head}")
endif()
file(STRINGS "${package}/licenses/libghostty-vt.txt" license LIMIT_COUNT 1)
if(NOT license STREQUAL "MIT License")
  message(FATAL_ERROR "Unexpected license in the packaged Ghostty SDK: ${license}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DEXE=${app}" "-DOBJDUMP=${Objdump}"
  -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/CheckDeps.cmake" COMMAND_ERROR_IS_FATAL ANY)

include("${CMAKE_CURRENT_LIST_DIR}/release/WindowsPowerShell.cmake")
execute_process(COMMAND ${zip_powershell} -File
    "${CMAKE_CURRENT_LIST_DIR}/check_package_markdown.ps1" -Root "${package}"
  WORKING_DIRECTORY "${package}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${app}" --version WORKING_DIRECTORY "${package}"
  OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT version STREQUAL "Termole ${Version}")
  message(FATAL_ERROR "Unexpected packaged executable version: ${version}")
endif()

if(NOT EXISTS "${Fzf}")
  message(FATAL_ERROR "Install fzf.exe on PATH and reconfigure to run the portable ZIP acceptance.")
endif()
function(run_check name)
  set(log "${record}/${name}.log")
  execute_process(COMMAND ${zip_powershell} -STA
    -File "${CMAKE_CURRENT_LIST_DIR}/release/${name}.ps1" -Package "${package}"
    -Evidence "${record}/${name}" ${ARGN} WORKING_DIRECTORY "${package}"
    RESULT_VARIABLE result OUTPUT_FILE "${log}" ERROR_FILE "${log}" TIMEOUT 300)
  if(NOT result STREQUAL "0")
    file(READ "${log}" output)
    message(FATAL_ERROR "${name} failed (${result}). Evidence: ${record}\n${output}")
  endif()
  message(STATUS "Passed ${name}: ${log}")
endfunction()
run_check(preflight)
run_check(sdk -Cmake "${CMAKE_COMMAND}" -Generator "${Generator}"
  -MakeProgram "${MakeProgram}" -Cc "${Cc}" -Cxx "${Cxx}")
run_check(default-shell)
run_check(fzf -Fzf "${Fzf}")
run_check(plugin-menu)
file(WRITE "${record}/acceptance.txt"
  "${filename}\nSHA-256: ${digest}\nSource: ${revision}\nPackage: ${package}\n"
  "Passed: archive checksum, license, imports, documents, CLI preflight, SDK ABI 3, default shell, fzf, QuickJS menus\n"
  "Human handoff: #53 (IME, foreground/background focus, first-time README use)\n")
message(STATUS "Verified ${filename} SHA-256 ${digest} from ${revision}. Evidence: ${record}")
