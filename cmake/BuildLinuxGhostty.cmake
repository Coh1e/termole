# Upstream ships Linux source and a separate offline dependency bundle.
include(FetchContent)
set(ghostty_release https://release.files.ghostty.org/libghostty-vt/1.0.1)
FetchContent_Declare(ghostty_source
  URL "${ghostty_release}/libghostty-vt-1.0.1.tar.gz"
  URL_HASH SHA256=333dfce97641ef9b536bb9e08907ad86abaaada50e8f7417b70d9284adef37aa
  SOURCE_SUBDIR termole-no-auto-build)
FetchContent_Declare(ghostty_deps
  URL "${ghostty_release}/libghostty-vt-1.0.1-deps.tar.gz"
  URL_HASH SHA256=91a4e10960a52bf5bf4b15aa2a2cddaff8a89d6cd903d3055bda4fd17de3356d
  SOURCE_SUBDIR termole-no-auto-build)
FetchContent_Declare(ghostty_zig
  URL https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz
  URL_HASH SHA256=1cbe9df9f27e6b78d14ccbca43b6703a404ef79ef1c463de901d7f088d4e2026
  SOURCE_SUBDIR termole-no-auto-build)
FetchContent_MakeAvailable(ghostty_source ghostty_deps ghostty_zig)
set(TERMOLE_GHOSTTY_VT_ROOT "${CMAKE_BINARY_DIR}/ghostty-install")

# Zig renames completed cache directories atomically. DrvFs can deny that
# rename even when the source tree and install prefix are writable.
set(ghostty_cache_args)
if(CMAKE_HOST_SYSTEM_VERSION MATCHES "[Mm]icrosoft|[Ww][Ss][Ll]")
  if(IS_ABSOLUTE "$ENV{XDG_CACHE_HOME}")
    set(ghostty_cache_root "$ENV{XDG_CACHE_HOME}")
  elseif(IS_ABSOLUTE "$ENV{HOME}")
    set(ghostty_cache_root "$ENV{HOME}/.cache")
  else()
    message(FATAL_ERROR "Set HOME or XDG_CACHE_HOME to a native WSL location for the Zig cache.")
  endif()
  string(SHA256 ghostty_cache_key "${CMAKE_BINARY_DIR}")
  list(APPEND ghostty_cache_args
    --cache-dir "${ghostty_cache_root}/termole/ghostty-zig/${ghostty_cache_key}")
endif()
execute_process(
  COMMAND "${ghostty_zig_SOURCE_DIR}/zig" build -Demit-lib-vt -Doptimize=ReleaseFast
          ${ghostty_cache_args}
          --system "${ghostty_deps_SOURCE_DIR}" -p "${TERMOLE_GHOSTTY_VT_ROOT}"
  WORKING_DIRECTORY "${ghostty_source_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)
