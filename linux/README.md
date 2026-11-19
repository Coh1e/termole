# Linux C core entry

This directory is intentionally separate from the Windows product build.

It builds only:

- `src/vt/termole_vt.c` (C ABI adapter)
- `tests/vt_adapter_test.c` (contract test)\n- `tests/posix_session_test.cpp` + `src/platform/posix_pty.cpp`（POSIX PTY + core 会话）\n- `tests/posix_interactive_test.cpp` + `src/platform/posix_session.cpp`（持续交互、resize、退出码）

against a Linux static `libghostty-vt.a`.

## Prerequisites

- Linux x86_64, CMake >= 3.20, C compiler
- `third_party/libghostty-vt/1.0.1-linux/include/ghostty/...`
- `third_party/libghostty-vt/1.0.1-linux/lib/libghostty-vt.a`

Build the static library from the pinned source as described in
[docs/cross-platform-build.md](../docs/cross-platform-build.md).

## Build

```bash
cmake -S linux -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

The test executable links the static `libghostty-vt.a`; add `-static` via
`CMAKE_EXE_LINKER_FLAGS` once the Linux static toolchain decision is finalized.

## Presets (WSL)

From `linux/`:

```bash
cmake --preset linux-static
cmake --build --preset linux-static --parallel
ctest --preset linux-static
```

Equivalent to the explicit static CMake commands above.