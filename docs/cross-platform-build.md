# 跨平台构建

静态交付与 CMake 选型见 [ADR 0016](adr/0016-portable-c-core.md)。

## 各平台的构建输入

- Windows 构建通过 CMake 校验包含 `ghostty-vt-static.lib` 的下载包，链接时使用 `-static -static-libgcc -static-libstdc++`。
- Linux 上使用 Zig 从固定版本的源码构建静态库 `libghostty-vt.a`。
- macOS 的 `macos-static` 预设使用官方 XCFramework 提供的静态库。
- Linux 交付物要避免依赖 glibc，需使用 musl 工具链。当前 WSL 的 glibc 静态构建用于功能验证。

Windows 可用 `-DTERMOLE_GHOSTTY_VT_ROOT=安装目录` 指向已有的 1.0.1 SDK。原生 x86_64 Linux 的 CMake 配置会获取源码、依赖包和 Zig 并编译静态库，也支持通过同一变量指定已安装的目标平台库。

## 固定输入

| 输入 | URL | SHA-256 |
| --- | --- | --- |
| libghostty-vt 1.0.1 源码 | `https://release.files.ghostty.org/libghostty-vt/1.0.1/libghostty-vt-1.0.1.tar.gz` | `333dfce97641ef9b536bb9e08907ad86abaaada50e8f7417b70d9284adef37aa` |
| Zig 0.17.0 linux x86_64 | `https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz` | `1cbe9df9f27e6b78d14ccbca43b6703a404ef79ef1c463de901d7f088d4e2026` |

libghostty-vt 1.0.1 的 `build.zig` 要求 Zig **0.17.0**；0.15.x 会直接拒绝。

## 手工准备 Linux 依赖

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

## Linux 静态 C 核心

从仓库根目录运行：

```bash
cmake --preset linux-static
cmake --build --preset linux-static --parallel
ctest --preset linux-static --output-on-failure
```

`linux-static` 预设构建核心、PTY 和 Session 回归测试。Windows 和 Linux 各自的
PTY 与 Session 后端共用 PTY 契约测试和交互会话契约测试。两个共享测试覆盖
启动、子进程输出与退出码，Session 测试也覆盖输入和 `resize` 调用。

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

`linux-musl` 预设覆盖无界面核心、PTY、Session 和 Unix 域套接字。
`linux-gui` 预设静态链接 SDL3 和 FreeType，通过系统 X11 库连接桌面。

### macOS 构建

`macos-static` 使用 [Ghostty 1.0.1 官方 XCFramework](https://release.files.ghostty.org/libghostty-vt/1.0.1/ghostty-vt-1.0.1.xcframework.zip)
中的 macOS 通用静态库。SDL3 和 FreeType 静态链接，Cocoa 等系统框架由
macOS 提供。在 macOS 上依次运行 `cmake --preset macos-static`、`cmake --build --preset macos-static` 和 `ctest --preset macos-static`。像素用例需要可用的图形桌面。

## Windows 构建

准备 CMake 3.28 或更新版本、Visual Studio Build Tools 的 NMake，以及 x64 MinGW-w64 GCC/G++ 和 windres。

以默认安装位置为例，在 x64 Native Tools Command Prompt 中设置工具路径后，从项目根目录运行：

```bat
set "MINGW_PREFIX=C:/msys64/mingw64"
set "PATH=%MINGW_PREFIX%\bin;%PATH%"
cmake --workflow --preset windows-mingw
```

`windows-mingw` 预设使用 `NMake Makefiles` 生成器。启动程序位于 `.local/build/nmake-debug/bin/termole.exe`。Windows 安装包和静态 SDK 见 [SDK 参考](sdk.md)。

### Windows 便携包验收

在本机 Windows 11 x64 使用同一工具环境运行：

```bat
cmake --workflow --preset windows-mingw-release
```

工作流先构建和运行已注册的测试，再用 CPack 生成 ZIP 和 `.sha256`。最后一步校验摘要，把 ZIP 解压到含中文和空格的新目录，并检查随包文件、文档链接和程序版本。它通过包内文件选择脚本核对空目录、无效目录和缺少 fzf 时的标准流与退出码。命令输出给出本次解压目录。复核已有 ZIP 可运行 `cmake --build --preset windows-verify-zip`。

在输出的解压目录打开 Windows PowerShell 5.1，按 [README 快速开始](../README.md#快速开始)打开默认 Shell 和文件选择窗口。随后完成以下桌面检查。

1. 在测试目录准备 `中文目标.txt` 和 `alpha.txt`，从前台 PowerShell 运行随包 `select-file.ps1 -Directory <测试目录>`。直接用中文输入法键入 `zhongwen`，选词“中文”，查看预览并按 Enter。核对返回的完整中文路径和 `$LASTEXITCODE`。从后台召出窗口时，记录窗口可见、点击后能输入的过程。保留候选窗和提交后的画面。
2. 在预览运行时分别用 Esc 和关窗取消，检查脚本输出、退出码和子进程回收。并发打开两个选择器，取消其中一个，再从另一个选回文件。冷启动使用固定的候选和窗口设置，从调用脚本计时至首个按键产生预期选择。预览边界和各情境的预期结果见 [Applet 规格](https://github.com/Coh1e/termole/issues/31)及 [AV1 验收](https://github.com/Coh1e/termole/issues/43)。
3. 启用随包 QuickJS 示例，选中两行中英文，执行菜单命令并粘贴到编辑器。用两个窗口确认命令读取打开菜单的视图，再停用插件并重启，检查菜单项消失。[配置示例](config.md#本地-quickjs-插件) · [菜单与剪贴板契约](https://github.com/Coh1e/termole/issues/44)

验收记录写明源提交、工作树改动、包名、SHA-256、系统与 fzf 版本、实测结果及未执行项。新包使用新的验收记录。

- Windows 标准包启用 `TERMOLE_WITH_QUICKJS=ON`。通用源码构建默认关闭。引擎及示例插件的契约测试为 `script_abi_stub`。
- 技术探针通过 `-DTERMOLE_BUILD_SPIKES=ON` 启用。fzf 相关测试按工具可用性注册，详见[验证指南](agents/testing.md)。
- `conventions_static_deps` 和 `conventions_release_size` 分别检查 Release 的动态依赖和体积。启用 QuickJS 时还会运行 `conventions_quickjs_size`。
- 下载缓存和构建产物放在项目的 `.local/` 下。第三方来源见[第三方声明](../THIRD_PARTY_NOTICES.md)。

## Windows ConPTY

`WindowsPty` 统一负责进程启动、环境、ConPTY、重定向和进程树生命周期。
`NativeSession` 通过它操作终端设备，继续负责 VT 状态与通知。
文件重定向、直接 stdio 句柄和纯终端三种路径仍由现有产品测试覆盖。
