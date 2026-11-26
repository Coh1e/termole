cmake_minimum_required(VERSION 3.28)
cmake_path(COMPARE "${Root}" EQUAL "${BuildDir}/tests/sdk-consumer" valid_root)
if(NOT valid_root OR NOT IS_DIRECTORY "${BuildDir}/CMakeFiles")
  message(FATAL_ERROR "Invalid SDK test build directory: ${Root}")
endif()
file(REMOVE_RECURSE "${Root}")
file(MAKE_DIRECTORY "${Root}")
set(package "${Root}/package")
set(artifacts "${Root}/artifacts")

function(run label directory)
  set(log "${Root}/${label}.log")
  execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${directory}"
    OUTPUT_FILE "${log}" ERROR_FILE "${log}" RESULT_VARIABLE result)
  if(NOT result STREQUAL "0")
    file(READ "${log}" output)
    message(FATAL_ERROR "${label} failed (${result}):\n${output}")
  endif()
endfunction()

run(install "${Root}" "${CMAKE_COMMAND}" --install "${BuildDir}" --prefix "${package}")
foreach(path IN ITEMS include/termole/termole.h include/termole/termole.hpp
    "lib/${Library}" lib/ghostty-vt-static.lib lib/cmake/termole/termoleConfig.cmake
    lib/cmake/termole/termoleConfigVersion.cmake lib/cmake/termole/termoleTargets.cmake
    share/termole/examples/sdk-host/CMakeLists.txt share/termole/examples/sdk-host/main.cpp
    share/termole/examples/sdk-host/README.md docs/README.md docs/cli.md docs/config.md docs/sdk.md
    docs/architecture.md docs/cross-platform-build.md docs/adr/README.md docs/adr/0007-console-launcher-wait.md)
  if(NOT EXISTS "${package}/${path}")
    message(FATAL_ERROR "Missing installed SDK file: ${path}")
  endif()
endforeach()

if(NOT PowerShell OR NOT EXISTS "${PowerShell}")
  message(FATAL_ERROR "t22_sdk_consumer requires Windows PowerShell to check installed Markdown links")
endif()
set(check_markdown "${PowerShell}" -NoProfile -ExecutionPolicy Bypass -File
  "${CMAKE_CURRENT_LIST_DIR}/check_package_markdown.ps1")
set(anchor_fixture "${Root}/markdown-anchor-fixture")
file(MAKE_DIRECTORY "${anchor_fixture}")
file(WRITE "${anchor_fixture}/README.md"
  "[valid](target.md#present)\n[invalid](target.md#missing)\n")
file(WRITE "${anchor_fixture}/target.md" "# Present\n")
execute_process(COMMAND ${check_markdown} -Root "${anchor_fixture}"
  RESULT_VARIABLE anchor_result OUTPUT_VARIABLE anchor_output ERROR_VARIABLE anchor_output)
string(STRIP "${anchor_output}" anchor_output)
if(NOT anchor_result STREQUAL "1" OR
    NOT anchor_output STREQUAL "README.md:2: target.md#missing: missing anchor #missing")
  message(FATAL_ERROR "Unexpected Markdown anchor check result (${anchor_result}):\n${anchor_output}")
endif()
run(markdown-links "${Root}" ${check_markdown} -Root "${package}")

function(build_consumer name source executable)
  set(build "${Root}/${name}-build")
  run("${name}-configure" "${Root}" "${CMAKE_COMMAND}" -S "${source}" -B "${build}"
    -G "${Generator}" "-DCMAKE_BUILD_TYPE=${Config}" "-DCMAKE_PREFIX_PATH=${package}"
    "-DCMAKE_CXX_COMPILER=${Cxx}" "-DCMAKE_MAKE_PROGRAM=${MakeProgram}"
    -DCMAKE_EXE_LINKER_FLAGS=-static -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
    -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF ${ARGN})
  run("${name}-build" "${Root}" "${CMAKE_COMMAND}" --build "${build}")
  if(NOT EXISTS "${build}/${executable}")
    message(FATAL_ERROR "Missing ${executable} in ${build}")
  endif()
endfunction()

build_consumer(consumer "${SourceDir}/tests/sdk-consumer" termole-sdk-consumer.exe
  "-DTERMOLE_PACKAGE_ROOT=${package}")
build_consumer(example "${package}/share/termole/examples/sdk-host" termole-sdk-host.exe)
run(consumer-run "${Root}/consumer-build" "${Root}/consumer-build/termole-sdk-consumer.exe"
  --helper "${Helper}" --artifacts "${artifacts}")
foreach(name IN ITEMS t22-sdk-view.png t22-sdk-host.png t22-sdk-view.stats.txt t22-sdk-host.stats.txt)
  if(NOT EXISTS "${artifacts}/${name}")
    message(FATAL_ERROR "Missing consumer evidence: ${name}")
  endif()
endforeach()
file(READ "${artifacts}/t22-sdk-view.stats.txt" stats)
if(NOT stats MATCHES "printWindow=true" OR NOT stats MATCHES "inkPixels=[0-9]+")
  message(FATAL_ERROR "Invalid view capture: ${stats}")
endif()
