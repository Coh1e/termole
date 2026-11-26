# Termole

Termole 可以打开一个轻量终端，也可以让脚本临时打开一个交互窗口。例如，PowerShell 可以把文件名交给 fzf，在窗口中搜索和预览，选中后把完整路径交回脚本。

## 快速开始

解压 Windows x64 便携包，在包含 `bin` 和 `share` 的目录打开 PowerShell。运行第一条命令打开默认 Shell。第二条启动 CMD 并让调用方等待，直到在新窗口中输入 `exit`。

~~~powershell
.\bin\termole.exe
.\bin\termole.exe -e cmd.exe /k ver
~~~

要体验文件选择，把独立安装的 `fzf.exe` 加入 `PATH`，在同一目录运行：

~~~powershell
.\share\termole\examples\select-file.ps1 -Directory .
~~~

输入文件名的一部分即可筛选。预览最多读取 16 KiB；按 Enter 返回选中文件的完整路径，按 Esc 取消。

## 产品基线

轻量终端面向 Windows、Linux 和 macOS，支持启动 Shell、运行已有 CLI/TUI、输入和复制粘贴。TUI Applet 优先在 Windows 验收，包括冷启动至可输入的耗时。[终端规格](https://github.com/Coh1e/termole/issues/32) · [Applet 规格](https://github.com/Coh1e/termole/issues/31)

Windows 标准 Release 构建内置 QuickJS。用户可通过配置启用本地插件，例如把选区复制为 Markdown 引用。[插件配置](docs/config.md#本地-quickjs-插件) · [扩展设计](docs/adr/0018-quickjs-plugins.md)

独立启动、`-e`、`--stdio`、配置、SDK ABI 3、本地会话、管道动作和已有脚本接口按兼容契约维护。原生视图的设计目标包括字体回退、缩放后的网格对齐、快速冷启动和低空闲资源占用。长期方向包括客户端与服务端分工、启动后定制和 Shell 集成。其他平台的 Applet、跨进程托管、预热和 Sixel 图片预览属于后续规划，SSH 会话管理器在产品范围外。[CLI](docs/cli.md) · [兼容参考](docs/README.md#兼容参考) · [原生视图定制](docs/adr/0017-native-embedding.md)

## 从源码构建

如果从 Windows 源码仓库开始，先按[跨平台构建说明](docs/cross-platform-build.md#windows-构建)准备工具，配置好 NMake 和 MinGW-w64 后，从项目根目录运行：

~~~bash
cmake --workflow --preset windows-mingw
~~~

构建完成后，在项目根目录的 PowerShell 中启动：

~~~powershell
.\.local\build\nmake-debug\bin\termole.exe
~~~

## 文档

从[文档索引](docs/README.md)查找命令行用法、设置、各平台构建、Windows SDK、产品设计、验证方法和当前规格。
