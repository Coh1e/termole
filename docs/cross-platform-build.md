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

## Linux 静态 C core

从仓库根目录运行：

```bash
cmake --preset linux-static
cmake --build --preset linux-static --parallel
ctest --preset linux-static --output-on-failure
```

Linux preset 构建核心、PTY 和 Session 回归测试。Windows 和 Linux 各自的
PTY 与 Session 后端共用 PTY 契约测试和交互会话契约测试。两个共享测试覆盖
启动、子进程输出与退出码，Session 测试也覆盖输入和 resize 调用。

`linux/` 下的独立入口仍兼容已有构建目录。当前静态 glibc 构建仍会报告
`getaddrinfo` 的 NSS 警告。测试使用本地 shell 和 PTY，未覆盖 DNS。

### musl 构建

另外下载 [Ghostty 1.0.1 依赖包](https://release.files.ghostty.org/libghostty-vt/1.0.1/libghostty-vt-1.0.1-deps.tar.gz)
（SHA-256 `91a4e10960a52bf5bf4b15aa2a2cddaff8a89d6cd903d3055bda4fd17de3356d`）。
在 Linux 文件系统解压源码、依赖包和 Zig，从 Ghostty 源码目录运行：

```bash
../zig-x86_64-linux-0.17.0/zig build -Demit-lib-vt \
  --system ../libghostty-vt-1.0.1-deps -Dtarget=x86_64-linux-musl \
  -Doptimize=ReleaseFast -p ../ghostty-musl
```

回到 Termole 根目录，把以下两个变量指向实际解压和安装路径：

```bash
export TERMOLE_ZIG_BIN=/path/to/zig-x86_64-linux-0.17.0
export TERMOLE_GHOSTTY_VT_MUSL_ROOT=/path/to/ghostty-musl
cmake --preset linux-musl
cmake --build --preset linux-musl --parallel
ctest --preset linux-musl --output-on-failure
readelf -d .local/build/linux-musl/session_ipc_linux
```

该 preset 覆盖 headless 核心、PTY、Session 和 Unix socket。SDL3 的 Linux X11
构建用静态链接的 SDL3 和 FreeType，通过系统 X11 库连接桌面。

### macOS 构建

`macos-static` preset 使用 [Ghostty 1.0.1 官方 XCFramework](https://release.files.ghostty.org/libghostty-vt/1.0.1/ghostty-vt-1.0.1.xcframework.zip)
中的 macOS 通用静态库。SDL3 和 FreeType 静态链接，Cocoa 等系统框架由
macOS 提供。CI 运行核心、PTY、会话连接和 SDL3 像素用例。

## Windows ConPTY

`WindowsPty` 统一负责进程启动、环境、ConPTY、重定向和进程树生命周期。
`NativeSession` 通过它操作终端设备，继续负责 VT 状态与通知。
文件重定向、直接 stdio 句柄和纯终端三种路径仍由现有产品测试覆盖。

原生 x86-64 Linux 的新构建会按通用 CPU 基线编译 VT 库。已有构建目录保留缓存的 TERMOLE_GHOSTTY_VT_ROOT，以便使用本地离线安装。要从旧缓存切换到按源码编译，运行 cmake --preset linux-gui -U TERMOLE_GHOSTTY_VT_ROOT。
