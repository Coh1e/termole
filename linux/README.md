# Linux static core

From the repository root on Linux x86_64 or Ubuntu WSL, with CMake 3.28 or
newer, GNU Make and a C/C++ compiler:

```bash
cmake --preset linux-static
cmake --build --preset linux-static --parallel
ctest --preset linux-static
```

This builds the C VT adapter and runs the VT, POSIX session, and interactive
PTY tests. It uses the pinned Linux static libghostty-vt archive. See
[the cross-platform build notes](../docs/cross-platform-build.md) for its
provenance and build instructions.

Existing builds configured from the `linux/` directory can still use the
standalone preset there.

## SDL3 window

Install the X11 headers and a monospace TrueType font. On Ubuntu:

```bash
sudo apt-get install pkgconf fonts-dejavu-core libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
  libxtst-dev libxinerama-dev libxkbcommon-dev
cmake --preset linux-gui
cmake --build --preset linux-gui --parallel
ctest --preset linux-gui --output-on-failure
.local/build/linux-gui/termole
```

Use `TERMOLE_FONT=/path/to/font.ttf` to override the default font. From the
repository root on macOS, use the `macos-static` preset in the same three CMake
commands. It builds the same SDL3 window and uses the official Ghostty macOS
static library slice.

See [local sessions](../docs/local-sessions.md) for the server/client roles.
