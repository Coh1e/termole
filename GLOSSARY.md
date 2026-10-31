# Termole

Termole 为现有 CLI/TUI 程序提供 Windows 终端界面，并允许其他应用嵌入该界面。

## Language

**会话（Session）**：
一次终端程序运行及其终端状态。会话可以在没有视图时继续存在。
_Avoid_: 窗口、页面

**视图（View）**：
呈现一个会话并接收用户终端交互的界面。销毁视图本身不等于终止会话。
_Avoid_: 会话、顶层窗口

**宿主（Host）**：
嵌入 Termole 的应用，负责其界面布局、会话寿命及向扩展开放的能力。
_Avoid_: 终端子进程

**嵌入（Embedding）**：
其他应用在自己的界面内使用 Termole 终端的能力，是首版必须交付的产品能力。
_Avoid_: 仅启动外部终端窗口

**Profile**：
一组可复用的终端启动设置。
_Avoid_: 插件、应用包

**选择器（Selector）**：
外部程序（如 fzf、rofi）自己读取候选、绘制选择界面并返回选择。
Termole 只提供终端与数据通道，不实现选择结果协议。
_Avoid_: 把选择器称为 Termole 的 picker 模式

**执行模式（Execute）**：
Termole 运行一个外部程序，把调用方 stdin/stdout 交给它，程序 UI 走 ConPTY；Termole 不解析结果。
_Avoid_: picker、选择器内置模式

**Pip**：
Termole 的鼹鼠品牌角色。
_Avoid_: 功能模块名
