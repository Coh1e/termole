# SDK host example

Build against an installed Termole development package:

```bat
cmake -S examples\sdk-host -B .local\sdk-host -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=D:\path\to\Termole-0.1.0-win64
cmake --build .local\sdk-host
```

This host uses only `termole.hpp`, the import library and the DLL. It owns the
top-level window and message loop; Termole owns the child View and Session.
