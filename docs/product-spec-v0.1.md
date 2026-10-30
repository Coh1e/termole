# Termole & Pip
## Product & Technical Design Specification

**Version:** 0.1  
**Platform:** Windows 10 1809+ / Windows 11  
**Language:** Modern C++23  
**License:** TBD  
**Status:** Initial Design

---

# 1. 产品概述

## 1.1 产品名称

**Termole**

名称由两个单词组合：

- **Term** — Terminal
- **Mole** — 鼹鼠

吉祥物：**Pip**，一只小鼹鼠。

### Slogan

**Small terminal. Endless possibilities.**

### 产品定位

> A lightweight, composable, embeddable terminal surface for Windows.

一个面向 Windows 的轻量、可组合、可嵌入的终端界面原语。

Termole 不追求成为功能最丰富的终端模拟器，而是成为其他 CLI/TUI 程序可以依赖的基础界面组件。

## 1.2 核心理念

**Composition over Monolith.**

组合优于单体。

传统终端通常通过不断增加功能形成完整应用：

```text
Terminal
├── SSH Manager
├── File Browser
├── Command Palette
├── Session Manager
├── Git Integration
├── Search
└── Plugin System
```

Termole 采用相反的设计：

```text
Termole + SSH
    → SSH Client

Termole + SSH + Session Manager
    → Multi-SSH Client

Termole + fzf + fd
    → File Picker

Termole + ripgrep + fzf
    → Search Tool

Termole + lazygit
    → Git Client
```

这些功能不属于 Termole。

Termole 只负责提供终端运行环境、终端界面和必要的桌面集成能力。

组合程序不需要针对 Termole 进行特殊适配。

---

# 2. 设计原则

## 2.1 Terminal as a Primitive

终端是一种基础 GUI 原语。

类似按钮、文本框、列表一样，Terminal View 应当可以作为独立组件被其他 Windows 应用使用。

## 2.2 CLI First

所有公开的启动行为都必须能够通过 CLI 表达。

例如：

```powershell
termole -- ssh user@host

termole -- nvim

termole -- lazygit

termole --float --center -- fzf
```

不依赖 GUI 配置界面。

## 2.3 Embeddable by Design

终端核心不得依赖顶层窗口。

必须分离：

- Terminal Session
- Terminal View
- Terminal Window

从而支持独立运行与嵌入式运行。

## 2.4 Small Core

不内置：

- SSH Session Manager
- File Manager
- Fuzzy Finder
- Git Client
- Command Palette
- Plugin Marketplace
- 通用工作区管理器

这些能力由独立应用提供。

## 2.5 Native First

优先使用 Windows 原生能力。

不依赖 Electron、WebView、Qt 或完整跨平台 GUI 框架。

## 2.6 Pay Only for What You Use

不需要的功能不应产生运行时成本。

例如：

- Daemon 默认不运行。
- 不启动多余的后台服务。
- 不预加载不必要的资源。
- 不使用的渲染特效不初始化。

## 2.7 Standards Before Extensions

优先支持标准 CLI、PTY 和 VT 协议。

避免为了 Termole 自身发明新的终端协议、Shell 语法或插件格式。

---

# 3. 产品能力

## 3.1 独立终端

```powershell
termole -- pwsh
```

启动普通终端窗口。

支持 PowerShell、CMD、WSL 和原生 Windows 控制台程序。

## 3.2 Applet 模式

```powershell
termole --float --center --size 80x24 -- fzf
```

将 TUI 程序呈现为独立的小型 Windows 窗口。

Applet 并不是新的程序类型。

它只是：

```text
CLI/TUI Program
      +
Terminal View
      +
Window Configuration
```

## 3.3 CLI 组合

```powershell
termole -- pwsh -NoProfile -Command 'fd --type f | fzf'
```

通过现有 Shell 组合程序。

Termole 不负责重新实现 Shell 管道。

## 3.4 原生嵌入

其他 Windows 应用可以直接创建 TerminalView。

例如：

```cpp
auto session = termole::Session::create({
    .command = L"lazygit"
});

auto view = termole::View::create(session);

view.attach(parent_hwnd);
```

以上为目标 API 草案。

宿主程序负责窗口布局。

Termole 负责终端渲染、输入和会话管理。

## 3.5 多实例

允许多个独立 Termole 实例运行。

每个实例默认拥有独立的进程、会话与窗口。

未来可以通过可选 Daemon 共享字体缓存及其他资源。

---

# 4. 技术架构

## 4.1 技术选型

| 层次 | 技术 |
|---|---|
| Language | C++23 |
| Compiler | MSVC / Clang-cl |
| Build | CMake + Ninja |
| Platform | Win32 |
| Pseudo Terminal | ConPTY |
| VT Engine | libghostty-vt |
| Font | DirectWrite |
| Rendering | Direct2D |
| IPC | Windows Named Pipes |
| Configuration | TOML |
| Testing | Catch2 / GoogleTest |

`libghostty-vt` 是首选终端核心，但必须先通过 Windows 构建、API 完整性和许可证评估。若其当前接口不足以支撑完整会话，应保留替换终端核心的能力。

## 4.2 分层架构

```text
┌───────────────────────────────────────┐
│             termole.exe               │
│                                       │
│  CLI Parser                           │
│  Configuration                        │
│  Application Lifecycle                │
├───────────────────────────────────────┤
│             libtermole                │
│                                       │
│  TerminalSession                      │
│  TerminalView                         │
│  TerminalRenderer                     │
│  TerminalInput                        │
├───────────────────────────────────────┤
│              Adapters                 │
│                                       │
│  Ghostty VT Adapter                   │
│  ConPTY Adapter                       │
│  DirectWrite Adapter                  │
│  Direct2D Renderer                    │
├───────────────────────────────────────┤
│             Windows API               │
└───────────────────────────────────────┘
```

### 关键要求

上层不得直接依赖 Ghostty 的内部数据结构。

通过适配器隔离第三方库。

终端渲染器不得直接管理子进程。

终端会话不得直接创建 Win32 顶层窗口。

---

# 5. 核心组件

## 5.1 TerminalSession

负责：

- 创建和管理 ConPTY
- 启动子进程
- 读取终端输出
- 发送终端输入
- 管理终端尺寸
- 维护终端状态
- 处理进程退出

不负责窗口绘制。

## 5.2 TerminalView

负责：

- 终端字符网格呈现
- 字体与字形缓存
- 光标
- 文本选择
- 鼠标输入
- 键盘输入
- IME
- DPI 缩放
- 局部刷新

TerminalView 不拥有顶层窗口。

## 5.3 TerminalWindow

负责：

- Win32 窗口生命周期
- 标题
- 图标
- 边框
- 位置与尺寸
- 焦点
- 最小化与最大化
- 任务栏集成

Window 组合 View，但不拥有终端协议逻辑。

## 5.4 TerminalRenderer

定义独立的渲染接口。

初期使用 Direct2D。

未来可以替换为软件渲染器或其他 GPU 后端。

不应将 Direct2D 类型暴露到公共 TerminalSession API。

---

# 6. CLI 设计

## 6.1 基本语法

```text
termole [OPTIONS] -- [COMMAND] [ARGS...]
```

`--` 之后的参数原样作为目标程序参数，不再由 Termole 解析。

## 6.2 参数设计

### Execution

```text
--cwd PATH
--env KEY=VALUE
--hold
```

### Geometry

```text
--size COLSxROWS
--position X,Y
--center
```

`--size` 使用字符网格单位，而不是像素。

### Appearance

```text
--font NAME
--font-size SIZE
--theme NAME
--opacity VALUE
```

### Window

```text
--float
--borderless
--title TEXT
--icon PATH
```

### Configuration

```text
--config PATH
--profile NAME
```

## 6.3 典型用例

普通终端：

```powershell
termole -- pwsh
```

SSH：

```powershell
termole -- ssh user@host
```

Git：

```powershell
termole -- lazygit
```

文件选择器：

```powershell
termole --profile picker -- pwsh -NoProfile -Command 'fd --type f | fzf'
```

编辑器：

```powershell
termole --profile editor -- nvim
```

---

# 7. Profile 设计

Profile 是一组可复用的启动配置。

不是插件，也不是应用包。

示例：

```toml
[profile.picker]
size = "80x20"
center = true
borderless = true
floating = true
font_size = 13
close_on_exit = true

[profile.editor]
size = "120x40"
font_size = 14
close_on_exit = false
```

调用：

```powershell
termole --profile picker -- fzf
```

配置优先级：

```text
Built-in Defaults
       ↓
User Configuration
       ↓
Named Profile
       ↓
CLI Arguments
```

CLI 参数具有最高优先级。

---

# 8. 嵌入式架构

## 8.1 Standalone

```text
termole.exe
    └── TerminalWindow
           └── TerminalView
                  └── TerminalSession
```

## 8.2 In-process Embedding

```text
Host Application
    └── libtermole
           ├── TerminalView
           └── TerminalSession
```

宿主通过库接口创建并嵌入 TerminalView。

这是 MVP 必须支持的能力。

## 8.3 Out-of-process Embedding

未来支持：

```text
Host Application
      │
      ├── IPC
      │     └── termole.exe
      │
      └── Embedded Terminal Surface
```

需要设计独立的嵌入协议。

不将跨进程 `SetParent(HWND)` 视为完整解决方案。

---

# 9. 终端兼容性

优先保证终端正确性。

必须重点测试：

- ANSI / VT 转义序列
- Alternate Screen
- Unicode
- CJK 双宽字符
- Combining Marks
- Emoji 与字体回退
- Windows IME
- 鼠标报告
- Bracketed Paste
- 终端窗口调整
- 光标形状
- 现代键盘协议

第一阶段兼容性目标：

```text
PowerShell
CMD
WSL
fzf
Neovim
Helix
lazygit
```

对于高级协议，应以实际兼容性测试结果决定实现优先级，而不是假设终端核心已经完整支持。

---

# 10. 性能设计

Termole 应当追求低启动延迟和低空闲资源消耗。

## 10.1 基本策略

- 按需创建资源
- 缓存字形
- 局部刷新
- 减少堆分配
- 避免不必要的后台线程
- 避免持续重绘
- 尽可能减少依赖

## 10.2 测量指标

需要建立自动化基准测试，持续记录：

- Cold Start Latency
- Warm Start Latency
- Idle Working Set
- Peak Memory
- Idle CPU Usage
- Terminal Output Throughput
- Frame Latency

初期不设未经验证的性能承诺。

以 Foot、Windows Terminal 和 Ghostty 的实际表现作为参考。

---

# 11. Daemon（后续版本）

借鉴 Foot Server Mode。

```powershell
termole daemon
```

其他进程通过 Named Pipe 发送启动请求。

Daemon 可以共享：

- 字体资源
- 字形缓存
- 其他适合共享的只读资源

但 Daemon 必须是可选的。

独立运行不能依赖 Daemon。

---

# 12. Pip 吉祥物设计

## 12.1 角色定位

**Pip — The little mole behind your terminal.**

Pip 是一只小鼹鼠。

性格：

- 好奇
- 安静
- 聪明
- 灵巧
- 略微调皮
- 喜欢钻进各种地方

它不代表某种具体功能，而代表 Termole 的产品精神。

## 12.2 视觉方向

关键词：

**Tiny / Curious / Friendly / Minimal / Original**

形象建议：

- 圆润、紧凑的身体
- 小而醒目的鼻子
- 短小的爪子
- 简洁的面部表情
- 易识别的剪影
- 适合低分辨率图标

避免过多细节。

应当能够适配：

- 16×16 图标
- 32×32 图标
- Windows 应用图标
- GitHub Avatar
- README 插画
- 网站 Logo
- 贴纸

## 12.3 品牌关系

```text
Termole
  └── Pip
```

Termole 是工具品牌。

Pip 是品牌角色。

两者可以单独出现。

Logo 不必每次都包含吉祥物。

## 12.4 设计原则

参考 Ghostty/Punch 的品牌与角色分离方式，但不复制其具体造型、配色、表情或视觉特征。

Pip 应建立独立的原创角色设计。

---

# 13. 项目结构

建议：

```text
termole/
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── apps/
│   └── termole/
│       ├── main.cpp
│       └── cli.cpp
│
├── include/
│   └── termole/
│       ├── session.hpp
│       ├── view.hpp
│       ├── window.hpp
│       └── renderer.hpp
│
├── src/
│   ├── core/
│   ├── platform/
│   │   └── windows/
│   ├── renderer/
│   ├── vt/
│   └── config/
│
├── tests/
│   ├── unit/
│   ├── integration/
│   └── compatibility/
│
├── examples/
│   ├── standalone/
│   └── embedded/
│
├── assets/
│   ├── icons/
│   └── pip/
│
└── docs/
    ├── architecture.md
    ├── cli.md
    └── embedding.md
```

---

# 14. MVP 规划

## Phase 0 — Technical Spike

目标：验证技术可行性。

- ConPTY 创建与运行
- Ghostty VT 库 Windows 集成
- 字符网格获取
- DirectWrite/Direct2D 渲染
- 基本输入输出
- 子进程生命周期

交付：能够运行 PowerShell 的最小窗口。

## Phase 1 — Terminal Core

- 完整输入输出路径
- 字体和 DPI
- 终端调整尺寸
- 光标
- 滚动
- 复制粘贴
- 基础 Unicode
- TUI 兼容性

交付：可以日常运行基础 CLI/TUI 程序。

## Phase 2 — Applet CLI

- 命令行参数
- 窗口尺寸
- 位置
- 外观
- Profile
- 退出行为

交付：可通过 CLI 启动独立 TUI 小窗口。

## Phase 3 — Embedding

- 独立 TerminalView
- 宿主窗口嵌入
- 生命周期管理
- 嵌入式示例程序

交付：其他 C++ 应用可以嵌入 Termole。

## Phase 4 — Optimization

- 启动优化
- 内存优化
- 字形缓存
- 局部刷新
- 自动化基准测试

交付：性能稳定的轻量终端。

---

# 15. 非目标

Termole 不计划成为：

- Windows Terminal 的完整替代品
- SSH 管理软件
- 终端复用器
- IDE
- 文件管理器
- Shell
- 应用启动器
- 插件平台

如果某项能力可以通过现有 CLI/TUI 程序实现，原则上不应在 Termole 中重复实现。

---

# 16. 项目愿景

Termole 希望让 Windows 上的 CLI/TUI 程序像普通桌面应用一样自然地存在。

用户可以通过简单组合获得不同工具：

```text
Termole + ssh
Termole + fzf
Termole + fd + fzf
Termole + lazygit
Termole + nvim
```

开发者也可以将 TerminalView 直接嵌入自己的应用。

**Termole 提供界面，其他程序提供能力。**

它不试图拥有整个工具生态，而是让现有工具更容易组合。

### 最终定位

**Termole — Small terminal. Endless possibilities.**

*With Pip, the little mole.*