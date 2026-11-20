# 跨平台静态构建笔记

## 约束

- 所有平台静态链接。
- Windows：vendored `ghostty-vt-static.lib`，`-static -static-libgcc -static-libstdc++`。
- Linux：官方不提供预编译；从固定源码用 Zig 构建静态 `libghostty-vt.a`。
- macOS：官方 XCFramework 静态库，后续接入。
- Linux 真正无 glibc 依赖需 musl 工具链；当前 WSL 验证使用 glibc 静态，仅作功能验证。

## 固定输入

| 输入 | URL | SHA-256 |
| --- | --- | --- |
| libghostty-vt 1.0.1 source | `https://release.files.ghostty.org/libghostty-vt/1.0.1/libghostty-vt-1.0.1.tar.gz` | `333dfce97641ef9b536bb9e08907ad86abaaada50e8f7417b70d9284adef37aa` |
| Zig 0.17.0 linux x86_64 | `https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz` | `1cbe9df9f27e6b78d14ccbca43b6703a404ef79ef1c463de901d7f088d4e2026` |

libghostty-vt 1.0.1 的 `build.zig` 要求 Zig **0.17.0**；0.15.x 会直接拒绝。

## WSL 构建

```bash
mkdir -p /tmp/termole-cross && cd /tmp/termole-cross
curl -LO https://release.files.ghostty.org/libghostty-vt/1.0.1/libghostty-vt-1.0.1.tar.gz
curl -LO https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz
tar -xzf libghostty-vt-1.0.1.tar.gz
tar -xf zig-x86_64-linux-0.17.0.tar.xz
cd libghostty-vt-1.0.1
../zig-x86_64-linux-0.17.0/zig build -Demit-lib-vt -Doptimize=ReleaseFast
```

预期产物（以实际 `zig-out` 为准）：

```
zig-out/lib/libghostty-vt.a
zig-out/include/ghostty/...
```

## POSIX PTY 静态验证（已通过）

```bash
cd /mnt/d/playground/home/dev/projects/termole
g++ -std=c++23 -static -static-libgcc -static-libstdc++ -I src \
    src/platform/posix_pty.cpp tests/posix_pty_test.cpp -lutil \
    -o /tmp/posix_pty_static
/tmp/posix_pty_static
```

结果：

```
PASS: spawn posix pty
PASS: child output arrives
PASS: child exit code preserved
PASS: posix pty
```

## 已验证（Linux 静态 C core）\n\n```\ncmake -S linux -B build-linux-static -DCMAKE_BUILD_TYPE=Release \\\n      -DCMAKE_EXE_LINKER_FLAGS=-static\ncmake --build build-linux-static --parallel\n./build-linux-static/vt_adapter_linux\n=> PASS: VT adapter contract\n```\n\n同一入口还构建 `posix_session_linux`（POSIX PTY + Linux C core）：\n\n```\nctest --test-dir build-linux-static --output-on-failure\n1/2 vt_adapter_linux .... Passed\n2/2 posix_session_linux .. Passed\n```\n\n- 产物：`ELF 64-bit LSB executable, x86-64, ... statically linked`\n- vendored 静态库 SHA-256：`c59b9938936f17c7543c3ebec1ddc6b800bace6ea67a38ba6027209348819ba5`\n- 链接时有 glibc `getaddrinfo` 静态警告；当前 core 测试不需要 DNS，最终 Linux 分发仍建议 musl。\n\n## 待办

1. 拿到 `libghostty-vt.a` 后，在 WSL 编译并运行 `src/vt/termole_vt.c` + `tests/vt_adapter_test.c`。
2. 把 Linux `.a`/headers 固定到 `third_party/libghostty-vt/1.0.1-linux/` 并记录 SHA-256。
3. 建立 Linux core 的 CMake 入口（独立于当前 Windows-only 产品目标）。
4. WSL 的 CMake 3.22 需升级到 ≥3.28，或提供 Linux 专用最小 CMake 入口。

## 统一 PTY 接口（进行中）

`src/platform/pty.h` 定义平台无关的 `Pty`：

```cpp
struct PtyOptions { arguments, working_directory, columns, rows };
class Pty {
  static std::unique_ptr<Pty> spawn(...);
  read / write / resize / wait / close;
};
```

- Linux：`Pty::spawn` 返回 POSIX PTY 后端（已接入 `PosixSession`）。
- Windows：占位实现返回明确错误；`NativeSession` 仍直接用 ConPTY，后续接入同一接口。
- Linux CTest 仍为 3/3 通过。
## Windows ConPTY backend for `platform::Pty`

`src/platform/windows_pty.h/.cpp` implements the same `Pty` contract with
`CreatePseudoConsole` + `CreateProcessW`:

- argv/cwd/cols/rows
- read/write
- `ResizePseudoConsole`
- wait/exit code
- job object kill-on-close

`tests/windows_pty_test.cpp` verifies spawn, resize, output and exit code.
Windows full suite: 47/47 passed.

`NativeSession` still owns its richer ConPTY path (file/handle redirection,
stdio data channel); it has not been migrated onto `platform::Pty` yet.