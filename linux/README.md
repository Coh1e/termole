# Linux 静态核心与图形窗口

在 Linux x86_64 或 Ubuntu WSL 中准备 CMake 3.28 或更新版本、GNU Make 和 C/C++ 编译器。按[跨平台构建指南](../docs/cross-platform-build.md#linux-静态-c-核心)从仓库根目录构建 C VT 适配器，运行 VT、POSIX 会话和交互式 PTY 测试。已有构建仍可使用 linux/ 目录中的独立预设。

## SDL3 窗口

在 Ubuntu 上安装 X11 开发头文件和等宽 TrueType 字体，再从仓库根目录运行构建命令。

```bash
sudo apt-get install pkgconf fonts-dejavu-core libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
  libxtst-dev libxinerama-dev libxkbcommon-dev
cmake --preset linux-gui
cmake --build --preset linux-gui --parallel
ctest --preset linux-gui --output-on-failure
.local/build/linux-gui/termole
```

设置 `TERMOLE_FONT=/path/to/font.ttf` 可覆盖默认字体。macOS 使用相同的配置、构建和测试命令，将预设换为 `macos-static`。该预设使用 Ghostty 官方 macOS 静态库构建 SDL3 窗口。

服务端启动与客户端重连见[本地会话](../docs/cli.md#本地会话)。
