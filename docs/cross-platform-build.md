# 跨平台静态构建笔记

静态交付与 CMake 选型见 [ADR 0016](adr/0016-portable-c-core.md)。

## 各平台的构建输入
- Windows：使用CMake 校验下载的 `ghostty-vt-static.lib`，`-static -static-libgcc -static-libstdc++`。
- Linux：官方不提供预编译；从固定源码用 Zig 构建静态 `libghostty-vt.a`。
- macOS：macos-static 构建预设 使用官方 XCFramework 静态库。
- Linux 真正无 glibc 依赖需 musl 工具链；当前 WSL 验证使用 glibc 静态，仅作功能验证。

Windows 可用 `-DTERMOLE_GHOSTTY_VT_ROOT=安装目录` 指向已有的 1.0.1 SDK。原生 x86_64 Linux 的 CMake 配置会获取源码、依赖包和 Zig 并编译静态库，也支持通过同一变量指定已安装的目标平台库。

## 固定输入

| 输入 | URL | SHA-256 |
| --- | --- | --- |
| libghostty-vt 1.0.1 源码 | `https://release.files.ghostty.org/libghostty-vt/1.0.1/libghostty-vt-1.0.1.tar.gz` | `333dfce97641ef9b536bb9e08907ad86abaaada50e8f7417b70d9284adef37aa` |
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
# 从仓库根目录运行
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

## Linux 静态 C 核心

从仓库根目录运行：

```bash
cmake --preset linux-static
cmake --build --preset linux-static --parallel
ctest --preset linux-static --output-on-failure
```

Linux 构建预设 构建核心、PTY 和 Session 回归测试。Windows 和 Linux 各自的
PTY 与 Session 后端共用 PTY 契约测试和交互会话契约测试。两个共享测试覆盖
启动、子进程输出与退出码，Session 测试也覆盖输入和 resize 调用。

在 WSL 上，源码和构建目录可位于 Windows 挂载盘。自动构建 Ghostty 时，
Zig 本地缓存放在 `$XDG_CACHE_HOME/termole/ghostty-zig` 下，默认使用
`$HOME/.cache/termole/ghostty-zig`。缓存根目录应位于 WSL 的 Linux 文件系统。

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

该 构建预设 覆盖 无界面核心、PTY、Session 和 Unix socket。SDL3 的 Linux X11
构建用静态链接的 SDL3 和 FreeType，通过系统 X11 库连接桌面。

### macOS 构建

`macos-static` 构建预设 使用 [Ghostty 1.0.1 官方 XCFramework](https://release.files.ghostty.org/libghostty-vt/1.0.1/ghostty-vt-1.0.1.xcframework.zip)
中的 macOS 通用静态库。SDL3 和 FreeType 静态链接，Cocoa 等系统框架由
macOS 提供。CI 运行核心、PTY、会话连接和 SDL3 像素用例。

## Windows 构建

准备 MSYS2 的 MinGW-w64 GCC/G++ 工具链、GNU Make 和 CMake 3.28 或更新版本。Windows CI 使用 MSYS2 的 `MINGW64` 环境，安装 `make` 和 `mingw-w64-x86_64-gcc`。

以默认安装位置为例，在 CMD 中设置工具路径后，从项目根目录运行：

```bat
set "MINGW_PREFIX=C:/msys64/mingw64"
set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
cmake --workflow --preset windows-mingw
```

默认使用 `windows-mingw` 构建预设 和 MSYS Makefiles，启动程序位于 `.local/build/mingw-debug/bin/termole.exe`。Windows 安装包和静态 SDK 见 [SDK 参考](sdk.md)。

生成 Windows Release ZIP 时，依次运行：

```bat
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release --parallel
ctest --preset windows-mingw-release
cmake --build --preset windows-mingw-release --target package
```

- 可选 QuickJS 引擎通过 -DTERMOLE_WITH_QUICKJS=ON 启用，验证入口为 ctest --preset windows-mingw -R script_abi_stub。
- 技术探针通过 -DTERMOLE_BUILD_SPIKES=ON 启用。fzf 相关测试按工具可用性注册，详见 [验证指南](agents/testing.md)。
- Release 的动态依赖与体积由 conventions_static_deps 和 conventions_release_size 检查，启用 QuickJS 时另有 conventions_quickjs_size。
- 下载缓存和构建产物放在项目的 .local。第三方来源见 [第三方声明](../THIRD_PARTY_NOTICES.md)。

## Windows ConPTY

`WindowsPty` 统一负责进程启动、环境、ConPTY、重定向和进程树生命周期。
`NativeSession` 通过它操作终端设备，继续负责 VT 状态与通知。
文件重定向、直接 stdio 句柄和纯终端三种路径仍由现有产品测试覆盖。
