option(TERMOLE_WITH_QUICKJS "Enable the embedded QuickJS script engine" OFF)

if(TERMOLE_WITH_QUICKJS)
  include(FetchContent)
  set(TERMOLE_QUICKJS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/quickjs/2026-06-04")
  FetchContent_Declare(termole_quickjs_source
    URL "${TERMOLE_QUICKJS_ROOT}/quickjs-2026-06-04.tar.xz"
    URL_HASH SHA256=b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a
  )
  FetchContent_MakeAvailable(termole_quickjs_source)

  set(quickjs_dir "${termole_quickjs_source_SOURCE_DIR}")
  add_library(termole_quickjs OBJECT
    "${quickjs_dir}/quickjs.c"
    "${quickjs_dir}/dtoa.c"
    "${quickjs_dir}/cutils.c"
    "${quickjs_dir}/libregexp.c"
    "${quickjs_dir}/libunicode.c"
  )
  set_target_properties(termole_quickjs PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
  target_compile_definitions(termole_quickjs PRIVATE _GNU_SOURCE CONFIG_VERSION="2026-06-04")
  if(MINGW)
    target_compile_definitions(termole_quickjs PRIVATE __USE_MINGW_ANSI_STDIO=1)
  endif()
endif()
