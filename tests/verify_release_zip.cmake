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
set(root "${Evidence}/${digest}/${run}/便携 验收")
file(MAKE_DIRECTORY "${root}")
file(ARCHIVE_EXTRACT INPUT "${Archive}" DESTINATION "${root}")
set(package "${root}/${stem}")
set(app "${package}/bin/termole.exe")
set(examples "${package}/share/termole/examples")
foreach(path IN ITEMS bin/termole.exe LICENSE THIRD_PARTY_NOTICES.md licenses/quickjs.txt
    licenses/libghostty-vt.txt docs/architecture.md share/termole/examples/select-file.ps1
    share/termole/examples/preview-file.ps1 share/termole/examples/markdown-quote.js
    share/termole/examples/markdown-quote.toml include/termole/termole.h
    lib/termole.a lib/ghostty-vt-static.lib lib/cmake/termole/termoleConfig.cmake
    share/termole/examples/sdk-host/CMakeLists.txt)
  if(NOT EXISTS "${package}/${path}")
    message(FATAL_ERROR "${filename} lacks ${path}")
  endif()
endforeach()

execute_process(COMMAND "${PowerShell}" -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_CURRENT_LIST_DIR}/check_package_markdown.ps1" -Root "${package}"
  WORKING_DIRECTORY "${package}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${app}" --version WORKING_DIRECTORY "${package}"
  OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT version STREQUAL "Termole ${Version}")
  message(FATAL_ERROR "Unexpected packaged executable version: ${version}")
endif()

# The documented preflight errors return before launching fzf. Give this child
# an empty search path so a developer's installed fzf cannot mask the last case.
function(check_preflight directory code expected_diagnostic)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" --
      "${PowerShell}" -NoProfile -ExecutionPolicy Bypass -File
      "${examples}/select-file.ps1" -Directory "${directory}"
    WORKING_DIRECTORY "${package}" RESULT_VARIABLE status OUTPUT_VARIABLE output
    ERROR_VARIABLE diagnostic TIMEOUT 15)
  if(NOT status STREQUAL code OR NOT output STREQUAL "" OR NOT diagnostic MATCHES "${expected_diagnostic}")
    message(FATAL_ERROR "Unexpected selection result for ${directory}: ${status} [${output}] [${diagnostic}]")
  endif()
endfunction()
file(MAKE_DIRECTORY "${root}/empty")
check_preflight("${root}/empty" 1 "No files in directory")
check_preflight("${root}/missing" 2 "Invalid directory")
check_preflight("${package}" 2 "fzf.exe was not found on PATH")
message(STATUS "Verified ${filename} SHA-256 ${digest}")
message(STATUS "Use this extracted candidate for interactive acceptance: ${package}")
