#!/usr/bin/bash
# Termole build entry (MSYS2 bash + GNU make).
#
#   ./build.sh              configure + build + test (Debug)
#   PRESET=windows-mingw-release ./build.sh
#   ./build.sh --configure  configure only
#   ./build.sh --build      build only
#   ./build.sh --test       test only

set -euo pipefail

if [ -z "${MSYSTEM:-}" ]; then
  echo "build.sh: run this from an MSYS2 shell (msys2_shell.cmd)." >&2
  exit 2
fi

cd "$(dirname "$0")"
preset="${PRESET:-windows-mingw}"
mode="${1:---all}"

# CMake is a host tool, not part of the MSYS2 tree. Prefer PATH, then the
# standard Windows install location; users can override with TERMOLE_CMAKE.
if ! command -v cmake >/dev/null 2>&1; then
  for dir in "/c/Program Files/CMake/bin" "/c/Program Files (x86)/CMake/bin"; do
    if [ -x "$dir/cmake.exe" ]; then
      PATH="$dir:$PATH"
      export PATH
      break
    fi
  done
fi
command -v cmake >/dev/null 2>&1 || {
  echo "build.sh: cmake not found; set TERMOLE_CMAKE to the CMake bin directory." >&2
  exit 2
}

case "$mode" in
  --configure) cmake --preset "$preset" ;;
  --build)     cmake --build --preset "$preset" --parallel ;;
  --test)      ctest --preset "$preset" --output-on-failure ;;
  --all)
    cmake --preset "$preset"
    cmake --build --preset "$preset" --parallel
    ctest --preset "$preset" --output-on-failure
    ;;
  *) echo "usage: $0 [--configure|--build|--test|--all]" >&2; exit 2 ;;
esac
