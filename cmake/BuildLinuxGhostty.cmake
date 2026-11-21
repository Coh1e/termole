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
execute_process(
  COMMAND "${ghostty_zig_SOURCE_DIR}/zig" build -Demit-lib-vt -Doptimize=ReleaseFast
          -Dtarget=x86_64-linux-gnu -Dcpu=baseline
          --system "${ghostty_deps_SOURCE_DIR}" -p "${TERMOLE_GHOSTTY_VT_ROOT}"
  WORKING_DIRECTORY "${ghostty_source_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)
