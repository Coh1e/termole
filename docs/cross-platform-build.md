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

正式候选从干净提交构建。使用已解锁的 Windows 11 x64 交互桌面，将 `fzf.exe` 加入 PATH，预留系统剪贴板给窗口测试。使用上述 MinGW 工具链时，从全 ASCII 路径的仓库运行，测试会为含中文和空格的包路径创建 SDK 目录联接。[SDK 路径说明](sdk.md#构建外部宿主)

从 Codex 或 PowerShell 7 运行同一预设时，ZIP 验收脚本使用 Windows PowerShell 5.1 自己的模块搜索路径。

工作流依次完成构建、CTest、CPack、SHA-256 校验，再将最终 ZIP 解压到新目录。验证程序读取包内许可证、文档链接、版本、依赖和源码修订号，从包内 SDK 编译运行纯 C 宿主，再运行以下窗口场景。

- 启动默认 Shell，检查子进程与窗口像素。
- 使用真实 fzf 检查选择、预览、取消、并发实例的独立性、标准流和子进程回收。五次冷启动分别记录第一个有效按键的耗时。[AV1 验收](https://github.com/Coh1e/termole/issues/43)
- 从当前视图的右键菜单执行 QuickJS 命令，核对粘贴到编辑器的文字、双窗口选区和停用后的菜单。[菜单与剪贴板契约](https://github.com/Coh1e/termole/issues/44)

控制台输出本次证据目录。成功记录位于 `<build>/tests/release-zip/<SHA-256>/<run>/acceptance.txt`，同目录保存每项的日志与截图。包内 `share/termole/source-revision.txt` 记录安装时的提交和工作树状态。验收会检查干净工作树的 HEAD 与包内修订号一致。无 Git 元数据的源码仍可构建和打包，修订号为 `unknown`。在相应提交的构建目录复核已有 ZIP，运行 `cmake --build --preset windows-verify-zip`。

人工项沿用 [#53 交接](https://github.com/Coh1e/termole/issues/53)。提供当次 ZIP 摘要和解压路径，由验收者回填真实输入法选词、前后台焦点和首次阅读 README 的结果。

- Windows 标准包启用 `TERMOLE_WITH_QUICKJS=ON`。通用源码构建默认关闭。引擎及示例插件的契约测试为 `script_abi_stub`。
- 技术探针通过 `-DTERMOLE_BUILD_SPIKES=ON` 启用。fzf 相关测试按工具可用性注册，详见[验证指南](agents/testing.md)。
- `conventions_static_deps` 和 `conventions_release_size` 分别检查 Release 的动态依赖和体积。启用 QuickJS 时还会运行 `conventions_quickjs_size`。
- 下载缓存和构建产物放在项目的 `.local/` 下。第三方来源见[第三方声明](../THIRD_PARTY_NOTICES.md)。

## Windows ConPTY

`WindowsPty` 统一负责进程启动、环境、ConPTY、重定向和进程树生命周期。
`NativeSession` 通过它操作终端设备，继续负责 VT 状态与通知。
文件重定向、直接 stdio 句柄和纯终端三种路径仍由现有产品测试覆盖。

## Windows 源码归档

在已配置的 Git 检出目录运行 `cmake --build --preset windows-mingw-release --target package_source`。CPack 从当前 `HEAD` 导出文件，产物位于构建目录的 `package/`。制作源码归档需要 Git。解压后可按前述 Windows 构建命令编译程序和 SDK。
