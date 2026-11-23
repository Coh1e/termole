# Termole

## 产品基线

Termole 让已有 CLI/TUI 进入桌面工作流。轻量独立终端用于启动 Shell、运行 TUI、输入和复制粘贴。TUI Applet 为一次任务打开交互窗口：外部程序负责交互，调用脚本提供输入、接收结果并继续工作流。

Windows 优先验收。首个 Applet 由 PowerShell 冷启动 fzf 浮窗，支持搜索、文本预览、选择和取消，并测量从调用到可输入的耗时。轻量终端须在 Windows、Linux 和 macOS 上验证基本交互。其他平台的 Applet、跨进程托管、预热和 Sixel 图片预览留待后续。[Windows Applet 规格](https://github.com/Coh1e/termole/issues/31)

插件宿主列入后续交付计划。Termole 计划通过嵌入的 QuickJS 执行 JavaScript 插件，以扩展终端工作流。接口边界见[QuickJS 插件与原生 SDK 的定位](docs/adr/0018-quickjs-plugins.md)。

长期架构借鉴 foot 的客户端／服务端分工，支持启动后精细定制。[原生视图定制](docs/adr/0017-native-embedding.md)规定每个视图的字体、字号、配色和内边距行为。Termole 不开发 SSH Sessions 管理器。

视图采用原生渲染和 Quiet Native 视觉，追求 Ghostty 式开箱即用的字体体验，包括系统字体发现、缺字回退和缩放后的网格对齐。性能目标包括快速冷启动、小分发体积、低空闲内存和接近零的空闲 CPU 占用。

插件宿主的交付方案采用静态内置 QuickJS 的标准包，并计划仅在配置显式启用本地插件时创建运行时。长期支持基于会话和视图的终端扩展。需要理解当前命令行的功能由相应 Shell 集成提供，CMD 的集成方式待定。

重构遵循[跨平台 C 核心与 CMake 决策](docs/adr/0016-portable-c-core.md)。独立启动、`-e`、`--stdio` 和两种场景使用的配置保持兼容，包括脚本依赖的字节、等待和退出状态。SDK ABI 3、本地会话重连、Pipe 动作和已有脚本接口按[兼容参考](docs/README.md#兼容参考)维护；停用、删除或破坏兼容须逐项确认。

## 快速开始

在 Windows 上，如果已经有 `termole.exe`，在它所在的目录打开 PowerShell：

~~~powershell
.\termole.exe
.\termole.exe -e cmd.exe /k ver
~~~

第一条打开默认 Shell。第二条打开一个运行 Windows 版本查询的独立窗口，在窗口中输入 `exit` 后，调用方的 PowerShell 继续执行。

## 从源码构建

如果从 Windows 源码仓库开始，先按[跨平台构建说明](docs/cross-platform-build.md#windows-构建)准备工具，配置好 MSYS2 工具链后，从项目根目录运行：

~~~bash
cmake --workflow --preset windows-mingw
~~~

构建完成后，在项目根目录的 PowerShell 中启动：

~~~powershell
.\.local\build\mingw-debug\bin\termole.exe
~~~

## 文档

从[文档索引](docs/README.md)查找命令行用法、设置、各平台构建、Windows SDK、产品设计、验证方法和当前规格。
