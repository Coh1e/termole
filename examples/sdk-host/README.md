# SDK host example

Build from an MSYS2 MinGW shell against an installed Termole development package:

```sh
cmake -S examples/sdk-host -B .local/sdk-host -G 'MSYS Makefiles' \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-static \
  -DCMAKE_PREFIX_PATH=D:/path/to/Termole-0.1.0-win64
cmake --build .local/sdk-host
```

The executable is `.local/sdk-host/termole-sdk-host.exe`.
It uses `termole.hpp` and the installed static SDK. The host owns the
top-level window and message loop; Termole owns the child View and Session. The
host decides the layout, focus and shutdown order. Termole does not read the
user's TOML configuration, does not write the clipboard unless the host calls
`termole_view_copy_selection`, and does not display dialogs.

The full contract is in the package's `docs/sdk.md` and `docs/host-api.md`
(repository: `docs/`). A host that needs every capability in one place can read
`tests/sdk-consumer/main.cpp`, which is built against an installed package by the
`t22_sdk_consumer` test.
