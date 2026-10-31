# Termole

Termole 为现有 CLI/TUI 程序提供 Windows 终端界面，并允许其他应用嵌入该界面。

## Language

**会话（Session）**：
一次终端程序运行及其终端状态。会话可以在没有视图时继续存在。
_Avoid_: 窗口、页面

**视图（View）**：
呈现一个会话并接收用户终端交互的界面。销毁视图本身不等于终止会话。
_Avoid_: 会话、顶层窗口

**嵌入宿主（Embedding Host）**：
使用 Termole SDK 在自己的界面内嵌入终端的应用，负责其界面布局、会话寿命及向扩展开放的能力。旧文中的“宿主”指此角色。
_Avoid_: 终端子进程、交互宿主

**交互宿主（Interaction Host）**：
Termole 独立入口为外部程序提供窗口、显示和键盘交互的角色；它不承担程序的数据处理或结果解释。
_Avoid_: 把交互宿主与嵌入宿主混用

**嵌入（Embedding）**：
其他应用在自己的界面内使用 Termole 终端的能力，是首版必须交付的产品能力。
_Avoid_: 仅启动外部终端窗口

**Profile**：
一组可复用的终端启动设置。
_Avoid_: 插件、应用包

**选择器（Selector）**：
外部程序（如 fzf）自己读取候选、绘制选择界面并返回选择。
Termole 只提供终端与数据通道，不实现选择结果协议。
_Avoid_: 把选择器称为 Termole 的 picker 模式

**执行模式（Execute，`-e`）**：
Termole 在 ConPTY 中运行外部程序，程序 fd 0/1/2 都接终端；不转发调用方 stdin/stdout。
_Avoid_: 把 `-e` 当成调用方数据通道

**标准流模式（Stdio，`--stdio`）**：
Termole 显式把调用方 stdin/stdout 接到外部程序，程序 UI 与 stderr 走 ConPTY；Termole 不解析结果。
_Avoid_: picker、选择器内置模式

**数据通道（Data Channel）**：
与终端显示分离、连接调用方与程序的字节路径。`--stdio` 是双向标准流通道；pipe 动作是终端到外部命令的单向文本通道。
_Avoid_: 把数据通道等同于终端画面或 picker 结果协议

**管道动作（Pipe action）**：
把当前视图、选区或回滚文本送到外部命令 stdin 的单向动作；命令 stdout/stderr 丢弃，结果不返回调用方。
_Avoid_: 把 pipe 动作称为 picker

**Pip**：
Termole 的鼹鼠品牌角色。
_Avoid_: 功能模块名
