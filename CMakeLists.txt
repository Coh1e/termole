cmake_minimum_required(VERSION 3.28)
project(Termole VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  include(CTest)
  include(cmake/TermolePosix.cmake)
  return()
endif()

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Termole currently targets Windows x64.")
endif()
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID
   AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"
   AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x86_64")
  message(FATAL_ERROR "Only the x64 compiler target is currently supported.")
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Keep generated artifacts and dependency caches inside the checkout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
option(TERMOLE_BUILD_C_GUI "Build the experimental C GUI role (Windows only)" ${WIN32})

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.local/fetchcontent" CACHE PATH
    "Project-local dependency cache")

include(CTest)
include(cmake/TermoleBuild.cmake)
include(cmake/TermoleQuickJS.cmake)

# libghostty-vt (x86_64-windows-gnu static, MPL-2.0).
set(TERMOLE_GHOSTTY_VT_ROOT "" CACHE PATH "Existing Windows libghostty-vt installation")
if(NOT TERMOLE_GHOSTTY_VT_ROOT)
  FetchContent_Declare(ghostty_windows
    URL https://release.files.ghostty.org/libghostty-vt/1.0.1/ghostty-vt-1.0.1-x86_64-windows-gnu.zip
    URL_HASH SHA256=8e2eb9e1822e85d786b456c8beb86f49d09c318c91f0c2913f91e7ffa12cda29
  )
  FetchContent_MakeAvailable(ghostty_windows)
  set(TERMOLE_GHOSTTY_VT_ROOT "${ghostty_windows_SOURCE_DIR}")
endif()
foreach(file IN ITEMS include/ghostty/vt.h lib/ghostty-vt-static.lib LICENSE)
  if(NOT EXISTS "${TERMOLE_GHOSTTY_VT_ROOT}/${file}")
    message(FATAL_ERROR "Incomplete Windows libghostty-vt installation: missing ${TERMOLE_GHOSTTY_VT_ROOT}/${file}")
  endif()
endforeach()
add_library(ghostty_vt STATIC IMPORTED)
set_target_properties(ghostty_vt PROPERTIES
  IMPORTED_LOCATION "${TERMOLE_GHOSTTY_VT_ROOT}/lib/ghostty-vt-static.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${TERMOLE_GHOSTTY_VT_ROOT}/include"
  INTERFACE_COMPILE_DEFINITIONS GHOSTTY_STATIC
  INTERFACE_LINK_LIBRARIES ntdll
)

option(TERMOLE_BUILD_APP "Build the Termole app, embedding host example and SDK" ON)
option(TERMOLE_BUILD_SPIKES "Build the feasibility probes (fetch third-party sources)" OFF)

if(TERMOLE_BUILD_APP)
  add_subdirectory(src)
  add_subdirectory(apps)
  if(BUILD_TESTING)
    add_subdirectory(tests)
  endif()

  install(TARGETS termole termole_app termole_build_options
    EXPORT termoleTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
  )
  install(FILES "$<TARGET_FILE:ghostty_vt>" DESTINATION lib)
  install(FILES "${TERMOLE_GHOSTTY_VT_ROOT}/LICENSE" DESTINATION licenses RENAME libghostty-vt.txt)
  if(TERMOLE_WITH_QUICKJS)
    install(FILES "${TERMOLE_QUICKJS_ROOT}/LICENSE" DESTINATION licenses RENAME quickjs.txt)
    install(FILES examples/markdown-quote.js examples/markdown-quote.toml
      DESTINATION share/termole/examples)
  endif()
  install(DIRECTORY include/termole DESTINATION include)
  install(FILES README.md GLOSSARY.md THIRD_PARTY_NOTICES.md LICENSE DESTINATION .)
  install(FILES docs/README.md docs/cli.md docs/config.md docs/sdk.md
    docs/cross-platform-build.md DESTINATION docs)
  install(DIRECTORY docs/adr DESTINATION docs FILES_MATCHING PATTERN "*.md")
  install(FILES docs/agents/testing.md DESTINATION docs/agents)
  install(EXPORT termoleTargets
    FILE termoleTargets.cmake
    NAMESPACE Termole::
    DESTINATION lib/cmake/termole
  )
  include(CMakePackageConfigHelpers)
  configure_package_config_file(cmake/termoleConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/termoleConfig.cmake"
    INSTALL_DESTINATION lib/cmake/termole
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/termoleConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/termoleConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/termoleConfigVersion.cmake"
    DESTINATION lib/cmake/termole
  )
  install(FILES examples/select-file.ps1 examples/preview-file.ps1 DESTINATION share/termole/examples)
  install(DIRECTORY examples/sdk-host DESTINATION share/termole/examples)
  set(CPACK_GENERATOR ZIP)
  set(CPACK_PACKAGE_NAME "Termole")
  set(CPACK_PACKAGE_VENDOR "Termole")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "Termole-${PROJECT_VERSION}-win64")
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  include(CPack)
endif()

if(TERMOLE_BUILD_SPIKES)
  option(TERMOLE_BUILD_PICKER_SPIKE "Validate fzf data/interaction channels" ON)
  option(TERMOLE_BUILD_NATIVE_SPIKE "Validate native child HWND and text rendering" ON)
  option(TERMOLE_BUILD_CORE_SPIKE "Opt into the Contour dependency comparison probe" OFF)
  if(TERMOLE_BUILD_PICKER_SPIKE)
    find_program(TERMOLE_FZF_EXECUTABLE NAMES fzf.exe)
    add_subdirectory(spikes/picker)
    if(NOT TERMOLE_FZF_EXECUTABLE)
      message(STATUS "fzf not found: picker executable will build; picker test is omitted.")
    endif()
  endif()
  if(TERMOLE_BUILD_NATIVE_SPIKE)
    add_subdirectory(spikes/native-view)
  endif()
  if(TERMOLE_BUILD_CORE_SPIKE)
    add_subdirectory(spikes/core/minimal)
  endif()
endif()
