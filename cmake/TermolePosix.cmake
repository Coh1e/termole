set(termole_root "${CMAKE_CURRENT_LIST_DIR}/..")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(termole_platform macos)
  include(FetchContent)
  FetchContent_Declare(ghostty_vt_xcframework
    URL https://release.files.ghostty.org/libghostty-vt/1.0.1/ghostty-vt-1.0.1.xcframework.zip
    URL_HASH SHA256=c0f9631277f726154c8f521954d17cdc41aac4626fc0df8cf925a19ee58e829b
  )
  FetchContent_MakeAvailable(ghostty_vt_xcframework)
  # FetchContent strips the archive's enclosing xcframework directory.
  set(ghostty_slice "${ghostty_vt_xcframework_SOURCE_DIR}/macos-arm64_x86_64")
  add_library(ghostty_vt STATIC IMPORTED)
  set_target_properties(ghostty_vt PROPERTIES
    IMPORTED_LOCATION "${ghostty_slice}/libghostty-vt.a"
    INTERFACE_INCLUDE_DIRECTORIES "${ghostty_slice}/Headers"
    INTERFACE_COMPILE_DEFINITIONS GHOSTTY_STATIC
  )
else()
  set(termole_platform linux)
  if(DEFINED TERMOLE_GHOSTTY_VT_ROOT AND TERMOLE_GHOSTTY_VT_ROOT STREQUAL "")
    message(FATAL_ERROR "Set TERMOLE_GHOSTTY_VT_MUSL_ROOT to the installed musl Ghostty prefix.")
  endif()
  # Migrate the default prefix cached by builds that used the vendored archive.
  cmake_path(SET old_prefix NORMALIZE "${termole_root}/third_party/libghostty-vt/1.0.1-linux")
  if(DEFINED TERMOLE_GHOSTTY_VT_ROOT)
    cmake_path(SET configured_prefix NORMALIZE "${TERMOLE_GHOSTTY_VT_ROOT}")
    if(configured_prefix STREQUAL old_prefix)
      unset(TERMOLE_GHOSTTY_VT_ROOT CACHE)
    endif()
  endif()
  if(NOT DEFINED TERMOLE_GHOSTTY_VT_ROOT)
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
      message(FATAL_ERROR "Set TERMOLE_GHOSTTY_VT_ROOT to a libghostty-vt installation for the target.")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/BuildLinuxGhostty.cmake")
  endif()

  unset(TERMOLE_GHOSTTY_VT_LIBRARY CACHE)
  find_library(TERMOLE_GHOSTTY_VT_LIBRARY
    NAMES libghostty-vt.a ghostty-vt-static.a
    PATHS "${TERMOLE_GHOSTTY_VT_ROOT}/lib" NO_DEFAULT_PATH)
  if(NOT TERMOLE_GHOSTTY_VT_LIBRARY)
    message(FATAL_ERROR
      "Linux libghostty-vt.a not found under ${TERMOLE_GHOSTTY_VT_ROOT}/lib. "
      "See docs/cross-platform-build.md.")
  endif()

  add_library(ghostty_vt STATIC IMPORTED)
  set_target_properties(ghostty_vt PROPERTIES
    IMPORTED_LOCATION "${TERMOLE_GHOSTTY_VT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${TERMOLE_GHOSTTY_VT_ROOT}/include"
    INTERFACE_COMPILE_DEFINITIONS GHOSTTY_STATIC
  )
endif()

find_package(Threads REQUIRED)

add_library(termole_core_${termole_platform} STATIC "${termole_root}/src/vt/termole_vt.c")
target_include_directories(termole_core_${termole_platform} PUBLIC
  "${termole_root}/include" "${termole_root}/src")
target_compile_features(termole_core_${termole_platform} PRIVATE c_std_11)
target_link_libraries(termole_core_${termole_platform} PUBLIC ghostty_vt m Threads::Threads)

add_library(termole_posix_pty STATIC
  "${termole_root}/src/platform/posix_pty.cpp"
  "${termole_root}/src/platform/pty.cpp")
target_include_directories(termole_posix_pty PUBLIC "${termole_root}/src")
target_compile_features(termole_posix_pty PUBLIC cxx_std_20)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(termole_posix_pty PUBLIC util)
endif()

add_library(termole_posix_session STATIC "${termole_root}/src/platform/posix_session.cpp")
target_link_libraries(termole_posix_session PUBLIC
  termole_posix_pty termole_core_${termole_platform})

add_library(termole_posix_ipc STATIC
  "${termole_root}/src/ipc/local_transport_posix.cpp"
  "${termole_root}/src/ipc/session_ipc.cpp"
  "${termole_root}/src/ipc/session_protocol.c")
target_link_libraries(termole_posix_ipc PUBLIC termole_posix_session)

option(TERMOLE_BUILD_SDL_GUI "Build the Linux/macOS SDL3 terminal window" OFF)
if(TERMOLE_BUILD_SDL_GUI)
  include("${CMAKE_CURRENT_LIST_DIR}/TermoleSDL.cmake")
endif()

if(BUILD_TESTING)
  function(add_posix_test name source library)
    add_executable(${name}_${termole_platform} "${termole_root}/tests/${source}")
    target_link_libraries(${name}_${termole_platform} PRIVATE ${library})
    add_test(NAME ${name}_${termole_platform} COMMAND ${name}_${termole_platform})
    set_tests_properties(${name}_${termole_platform} PROPERTIES TIMEOUT 15)
  endfunction()

  add_posix_test(vt_adapter vt_adapter_test.c termole_core_${termole_platform})
  add_posix_test(pty_contract pty_contract_test.cpp termole_posix_pty)
  add_posix_test(posix_session posix_session_test.cpp termole_posix_session)
  add_posix_test(posix_interactive posix_interactive_test.cpp termole_posix_session)
  add_posix_test(session_contract session_contract_test.cpp termole_posix_session)
  add_posix_test(session_ipc session_ipc_test.cpp termole_posix_ipc)
endif()
