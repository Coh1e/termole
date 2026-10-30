cmake_minimum_required(VERSION 3.28)
project(Termole VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Termole currently targets Windows x64.")
endif()
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
  message(FATAL_ERROR "Only the x64 compiler target is currently supported.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Keep generated artifacts and dependency caches inside the checkout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
# FetchContent is the single dependency-acquisition path for all optional
# sources. The product targets currently need no fetched dependency, so a
# clean product configure performs no network access.
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.local/fetchcontent" CACHE PATH
    "Project-local dependency cache")

include(CTest)
include(cmake/TermoleBuild.cmake)

option(TERMOLE_BUILD_APP "Build the Termole app, embedding host example and SDK" ON)
option(TERMOLE_BUILD_SPIKES "Build the feasibility probes (fetch third-party sources)" OFF)

if(TERMOLE_BUILD_APP)
  add_subdirectory(src)
  add_subdirectory(apps)
  if(BUILD_TESTING)
    add_subdirectory(tests)
  endif()

  install(TARGETS termole
    EXPORT termoleTargets
    RUNTIME DESTINATION bin COMPONENT Runtime
    LIBRARY DESTINATION bin COMPONENT Runtime
    ARCHIVE DESTINATION lib COMPONENT Development
  )
  install(TARGETS termole_app termole_host termole_picker
    RUNTIME DESTINATION bin COMPONENT Runtime
  )
  install(DIRECTORY include/termole DESTINATION include COMPONENT Development)
  install(FILES "$<TARGET_FILE:termole>" DESTINATION bin COMPONENT Development)
  install(FILES README.md THIRD_PARTY_NOTICES.md LICENSE DESTINATION . COMPONENT Runtime)
  install(FILES docs/cli.md docs/config.md docs/host-api.md docs/sdk.md docs/picker.md
    docs/t17-validation.md docs/t18-t19-validation.md docs/t20-resources.md
    docs/t21-performance.md docs/t24-acceptance.md DESTINATION docs COMPONENT Runtime)
  install(EXPORT termoleTargets
    FILE termoleTargets.cmake
    NAMESPACE Termole::
    DESTINATION lib/cmake/termole
    COMPONENT Development
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
    COMPONENT Development
  )
  install(DIRECTORY examples/sdk-host DESTINATION share/termole/examples COMPONENT Development)
  set(CPACK_GENERATOR ZIP)
  set(CPACK_PACKAGE_NAME "Termole")
  set(CPACK_PACKAGE_VENDOR "Termole")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "Termole-${PROJECT_VERSION}-win64")
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
  set(CPACK_COMPONENTS_ALL Runtime Development)
  set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Termole runtime")
  set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "Termole development")
  set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Termole executables, DLL, help and licence")
  set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION "Headers, import library, CMake package and SDK example")
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
