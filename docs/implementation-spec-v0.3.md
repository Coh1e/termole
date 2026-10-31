# Termole 首版实施规格 v0.3

状态：2026-10-31 用户已确认方向。v0.3 采用 foot 语义的 `-e`、显式 `--stdio` 和单向 pipe 动作，并由同一 `termole.exe` 的控制台 launcher / GUI 双角色提供默认等待；实现细节以任务和 ADR 为准。

## Problem Statement

Windows 用户希望将现有 CLI/TUI 程序当作自然的桌面工具使用：既可以打开普通终端，也可以把终端与外部的 fzf 等程序组合，而不要求 Termole 理解这些程序的候选或结果语义。Windows 应用开发者同时需要在自己的界面中嵌入同样的终端能力，而不必自行实现终端协议、子进程管理、文字绘制和输入处理。

Termole 需要提供小而可靠的终端界面基础组件，而不是再实现 SSH 管理器、文件管理器或 Shell。现有技术验证分别证明了部分可行性，尚未形成真实终端或可交付的嵌入 SDK。

## Solution

交付 Windows 原生终端应用、进程内嵌入 SDK，以及通用的外部程序集成边界。独立应用和嵌入宿主复用同一 Session/View 实现；所有公开启动行为均能用 CLI 表达。

Termole 遵循 foot 的设计分离：终端拥有 PTY、屏幕、回滚、选择与输入；外部程序拥有自己的候选、UI 和结果。Termole 不内置 picker，不解析选择结果。`-e` 与 foot 一样只把程序接入 PTY；需要调用方管道时使用显式的 `--stdio` 扩展，或由 shell/pipe 动作组合。

独立入口由一个控制台 launcher 和同一 `termole.exe` 的 GUI 角色组成：普通启动立即返回，不阻塞调用 shell；`-e` 和 `--stdio` 默认等待程序结束并返回退出码。`--stdio` 把调用方 stdin/stdout 接到程序，程序 UI 与 stderr 走 ConPTY；Termole 自身诊断走调用方 stderr。该入口方案见 [ADR 0007](https://github.com/Coh1e/termole/blob/main/docs/adr/0007-console-launcher-wait.md)。

首版采用 C++23，支持 Windows 10 22H2 / Windows 11 x64，通过 CMake、FetchContent、Ninja 构建。参考 Ghostty 的设计思路，开箱即用优先。优先借鉴成熟开源方案；直接引入模块若牵连非必要依赖，研究其实现并完成所需部分，保留来源及许可义务。

首个完整功能切片应同时证明：同一终端组件能在独立窗口和最小 C++ 宿主中运行 PowerShell，输入输出、调整尺寸和退出清理真实可用。之后扩展到通用 execute、显式 stdio、launcher 等待、pipe 动作、配置、兼容性和完整交付；单个切片完成不等于首版全部完成。

## User Stories

1. As a Windows 用户, I want 无配置启动 Termole, so that 我可以直接获得可用终端。
2. As a Windows 用户, I want 默认使用已安装的 PowerShell 7、否则使用 Windows PowerShell, so that 我无需先安装额外 Shell。
3. As a CLI 用户, I want 指定目标程序及参数, so that 我能运行现有控制台工具。
4. As a CLI 用户, I want 保留目标参数中的空格、引号、空参数和 Unicode, so that 启动行为不会因 Termole 包装而改变。
5. As a CLI 用户, I want 指定工作目录和环境变量, so that 程序能在预期环境运行。
6. As a CLI 用户, I want 通过现有 Shell 组合管道, so that 我无需学习 Termole 专用语法。
7. As a 用户, I want 同时运行多个独立实例, so that 各工具可以互不干扰地工作。
8. As a 用户, I want 指定字符网格大小、窗口位置或居中, so that TUI 能以适合任务的尺寸呈现。
9. As a 用户, I want 浮动、无边框、标题和图标设置, so that CLI/TUI 能呈现为独立的小工具。
10. As a 用户, I want 调整字体、字号、主题和透明度, so that 终端适合我的阅读和使用环境。
11. As a 用户, I want 默认使用系统自带等宽字体并跟随系统主题, so that 无需额外下载即可阅读终端。
12. As a 用户, I want 中文和 Emoji 有字体回退, so that 字体缺字不会使常见输出不可读。
13. As a 用户, I want 中文输入法正常工作, so that 我能输入候选文字而不破坏终端快捷键。
14. As a 用户, I want 双宽字符、组合字符和 Emoji 正确占位, so that 后续字符、光标和选择不会错位。
15. As a TUI 用户, I want alternate screen 正常切换, so that 全屏程序退出后能恢复原有终端画面。
16. As a TUI 用户, I want 鼠标报告和键盘输入遵循当前终端模式, so that 现有程序不需要 Termole 专用适配。
17. As a 用户, I want 选择、滚动、复制和粘贴, so that 我能在终端与其他应用间使用文本。
18. As a 用户, I want 粘贴遵循 bracketed paste 模式, so that TUI 能区分粘贴与普通按键。
19. As a 用户, I want 调整窗口后同步终端尺寸, so that Shell 和全屏程序能重新布局。
20. As a 用户, I want 只有状态变化才触发必要绘制, so that 空闲终端不会持续消耗 CPU。
21. As a 用户, I want 正常退出默认关闭窗口、失败退出保留最后画面和退出码, so that 我能看清失败原因。
22. As a 用户, I want 显式控制退出后是否保留窗口, so that 固定启动方式能覆盖默认策略。
23. As a 用户, I want 关闭窗口时结束该会话进程树, so that 不留下无主后台程序。
24. As a 用户, I want 退出时保留最后输出, so that 错误信息不会因提前报告完成而丢失。
25. As a 用户, I want 用 TOML 和 Profile 复用启动设置, so that 常用工具不需要每次输入全部参数。
26. As a 用户, I want CLI 覆盖 Profile、Profile 覆盖用户配置、用户配置覆盖默认值, so that 临时启动行为可以预测。
27. As a 用户, I want 无配置文件仍能启动, so that 第一次使用不需要初始化步骤。
28. As a 用户, I want 配置错误显示文件和位置并允许改用默认配置启动, so that 我能继续使用并修复配置。
29. As a CLI 用户, I want 参数错误给出明确诊断和失败状态, so that 自动化脚本不会误以为启动成功。
30. As a CLI 用户, I want 用 `-e` 在终端中运行程序并把 PTY 交给它, so that 行为与 foot 的 execute 思路一致，不偷偷转发调用方管道。
31. As a 脚本作者, I want 用显式 `--stdio` 把调用方 stdin/stdout 接到外部程序, so that 我能把候选送进 fzf 等程序并拿回原样结果，同时 Termole 不解析结果。
32. As a 用户, I want 用 pipe-visible/pipe-selected/pipe-scrollback 把终端文本交给外部命令, so that 我能在命令内部组合 fzf/fuzzel 等 UI 选择器并执行动作。
33. As a 脚本作者, I want `--stdio` 只转发标准字节和退出码, so that Termole 不追加换行、不转码、不排序、不把绘制混进结果。
34. As a 脚本作者, I want NUL、Unicode、内嵌换行和超大输入在 `--stdio` 下保持原样, so that 特殊候选项和大文件树不会被拆坏。
35. As a 用户, I want fzf 等选择器只在外部命令内部拥有选择语义, so that Termole 不变成 picker，也不限制只有 fzf 能用。
36. As a 用户, I want 外部工具缺失或外部命令失败时有明确诊断, so that 我能区分 Termole 故障和工具故障。
37. As a 维护者, I want 明确区分 foot 语义的 `-e`、Termole 扩展 `--stdio` 和单向 pipe 动作, so that 后续实现不会把三者混成一个 picker。
38. As a C++ 宿主开发者, I want 在自己的 HWND 下创建原生终端子窗口, so that 终端可以加入现有应用布局。
39. As a 宿主开发者, I want 自己控制布局、焦点和生命周期, so that 终端组件不会接管顶层应用。
40. As a 宿主开发者, I want 独立创建 Session 和 View, so that 我能控制运行中的程序和界面各自的寿命。
41. As a 宿主开发者, I want 销毁 View 后 Session 继续消费输出, so that 切换页面不会停止程序或阻塞输出。
42. As a 宿主开发者, I want 重新附加 View 后恢复当前画面和有界滚屏, so that 页面重建不丢失会话状态。
43. As a 宿主开发者, I want 创建多个独立 Session/View, so that 我能自行组织多个终端组件。
44. As a 宿主开发者, I want DLL 提供 C ABI 和 C++23 包装, so that 我能通过清晰边界使用终端能力。
45. As a 宿主开发者, I want ABI 版本不兼容时明确失败, so that 组件不会带着错误布局继续运行。
46. As a 宿主开发者, I want 库明确分配和释放规则, so that 不同运行时之间不会错误释放内存。
47. As a 宿主开发者, I want 操作返回结构化错误而不跨 C ABI 抛异常, so that 我能按应用需要处理错误。
48. As a 宿主开发者, I want 会话状态、尺寸、标题、选择文本和退出码查询, so that 我能实现必要的应用交互。
49. As a 宿主开发者, I want 标题、响铃、退出和错误事件经可调度队列交付, so that 后台 I/O 不会直接进入 UI 逻辑。
50. As a 宿主开发者, I want 控件不自行弹错误对话框, so that 错误展示符合宿主的产品流程。
51. As a 宿主开发者, I want 原生接口与未来脚本共享能力契约, so that 接入脚本时无需再实现一套行为。
52. As a 宿主开发者, I want 由宿主明确授权资源和操作, so that 未来脚本无法自行扩大权限。
53. As a 用户, I want 主动复制粘贴可用但终端输出默认不能写剪贴板, so that 本地和远端程序遵循一致规则。
54. As a 用户, I want 解压便携包即可运行, so that 无需安装编译器或开发工具。
55. As a 开发者, I want 最小宿主示例、接口文档和开发包, so that 我能独立验证嵌入能力。
56. As a 维护者, I want 记录开源实现来源、固定版本和许可义务, so that 后续维护和分发可追溯。
57. As a 维护者, I want 产品构建与比较探针依赖分离, so that 可行性研究不会把大依赖树自动带入产品。
58. As a 维护者, I want 用固定程序版本和可复现用例验收, so that 功能可用性不依赖主观判断。
59. As a 维护者, I want 测量启动、空闲资源和输出表现, so that 优化建立在真实数据而非未经验证的承诺上。
60. As a 脚本作者, I want `-e`/`--stdio` 默认等待程序结束并返回退出码, so that 我能在 CMD/PowerShell 中顺序安排后续命令。

## Implementation Decisions

### 平台、构建与复用

- Termole 自有代码使用 C++23，仅面向 Windows 10 22H2 / Windows 11 x64；优先使用 MSVC / clang-cl，统一 CMake、FetchContent、Ninja 入口。
- 编译器及 SDK 可以使用已有安装；依赖源码、工具、配置和缓存尽量留在项目内，避免改动全局环境。依赖固定版本或提交。
- C++ 方案优先；没有合适方案时可消费成熟 C 库。引入 Zig 等额外工具链需单独决定。
- 成熟方案不等于完整库依赖。遇到非必要的依赖扩张，研究成熟实现并完成所需部分；移植、改编代码保留来源及必要声明。优先 MIT/BSD/Apache-2.0，其他许可单独讨论。
- 项目采用 MIT。产品依赖、比较探针依赖和参考来源分别记录。
- Ghostty 是设计思路参考；Contour 当前是可选比较探针和成熟行为参考，不是已选定的产品核心；libghostty-vt 也未被选定。
- 正式 VT 核心、最小移植范围及渲染适配细节在首个完整切片中以依赖审计与真实行为证据落实，不能以探针通过替代实现决策。

### 终端与嵌入边界

- Session 管理 ConPTY、目标进程树、终端状态、输入输出、尺寸与退出，不创建顶层窗口。
- View 是宿主 UI 线程上的原生子窗口，负责显示和交互，不拥有顶层窗口；独立版由 Window 外壳组合相同的 Session/View。
- 首版一个 Session 最多附加一个 View，允许多组独立 Session/View。
- 销毁 View 不终止 Session；无 View 时继续消费输出，维护有界历史，重新附加后恢复画面。独立窗口关闭由应用层显式结束会话。
- 默认清理本会话启动的进程树，不支持子进程脱离会话继续运行。宿主退出后的会话保活不属于首版。
- 正常退出先排空输出再交付退出事件；对子进程持有句柄等情况须制定有界收尾策略，避免无限等待。容量及截止策略在相应任务中明确并验证。
- 渲染不管理子进程；终端引擎及 Direct2D 内部类型不进入公开 Session 契约。初始绘制方向为 DirectWrite/Direct2D，不预先构建多个渲染后端。

### 宿主能力契约

- 交付 DLL、C ABI、C++23 包装和最小 C++ 宿主示例。接口包含版本检查和明确的调用约定、线程及数据所有权规则。
- 宿主能力 IR 是类型化操作、查询与事件契约，不是自定义脚本语言、字节码、通用 VM 或首版跨进程协议。
- 操作涵盖会话创建/终止、发送文本或按键、调整尺寸、视图创建/销毁、外观、滚动、选择、复制粘贴。
- 创建设置包含程序、参数、工作目录和环境变量。查询包含状态、尺寸、标题、选择文本和退出码。事件包含标题变化、响铃、退出及错误。
- 输入数据异步保留时由库复制；库分配的返回数据通过配套函数释放；C ABI 不传播异常。C++ 包装复用同一行为，不创建第二套业务规则。
- View 由 UI 线程操作，终端 I/O 在内部完成；事件经宿主可调度队列交付，明确销毁与排队事件的交互，不把失效对象重新暴露给宿主。
- 库只报告结构化错误，不自行弹窗；独立版和嵌入宿主分别决定展示。
- 首版完成原生契约，暂不接 QuickJS。后续引入脚本引擎必须开放实际宿主能力，并由宿主限制会话、进程、剪贴板等权限；不提供通用文件/网络扩展 API。
- `--stdio` 和 pipe 动作是独立入口/应用层能力；嵌入宿主使用公开查询（选区、滚屏）或自己的数据通道，库不替宿主解析选择结果。

### CLI、配置、入口和退出

- 所有公开启动行为均可通过 CLI 表达；分隔符之后不再解析 Termole 参数。保留目标程序逻辑参数，Windows 命令行转义与标准句柄继承需要真实验证。
- CLI 覆盖执行目录/环境、保留窗口、字符网格尺寸、位置/居中、字体/字号/主题/透明度、浮动/边框/标题/图标、配置路径和 Profile。
- 组合管道交给目标 Shell，不实现 Shell 管道或命令语言。
- 零配置默认优先 PowerShell 7，否则 Windows PowerShell；系统等宽字体加中文/Emoji 回退，主题跟随系统。
- TOML 与 Profile 共用设置模型，优先级为默认值、用户配置、Profile、CLI。嵌入宿主直接传设置，不强制读取用户配置。
- 缺少配置可正常启动；已有配置出错显示文件和位置，提供使用默认值启动的选择；CLI 参数错误给出诊断及失败状态。
- 普通终端正常退出默认关闭，非零退出保留画面及状态；显式设置可覆盖。嵌入版退出展示由宿主决定。
- `termole.exe` 是控制台 launcher，同一 exe 以内部 `--gui` 角色承载窗口、Session 和 View。普通模式 launcher 成功启动 GUI 后返回 `0`，不等待窗口；`-e`/`--stdio` launcher 等待 GUI/程序结束并返回退出码。
- `--stdio` 要求调用方 stdin/stdout 是管道或文件重定向；stdin/stdout 为控制台时返回用法错误 `2`。`-e` 不要求重定向。
- 退出码：程序正常/失败退出时转交程序退出码；用户在程序退出前关闭窗口返回 `130`；程序启动失败 `1`；用法错误 `2`；Termole 基础设施失败 `125`。不定义 picker 专用码。
- 普通模式的 GUI 退出码不反向传给已返回的 shell；需要退出码时使用 `-e`/`--stdio`。

### 外部程序与数据通道（foot 设计对齐）

- `-e <program> [args...]` 采用 foot 的 execute 语义：程序在 ConPTY 中运行，fd 0/1/2 都接终端；Termole 不把调用方 stdin/stdout 透传给子进程，也不解析任何程序输出。现有 `--` 目标分隔符保留，`-e` 是其 foot 兼容别名。
- `--stdio <program> [args...]` 是 Termole 的显式扩展，不是 foot 行为：调用方 stdin/stdout 接到程序，程序 stderr/UI 走 ConPTY；Termole 自身诊断走调用方 stderr。它适用于任意程序，不要求 fzf；Termole 不读候选、不解析结果、不追加换行或转码。
- `--stdio` 的字节、背压和退出状态由继承的调用方句柄直接传递；调用方关闭 stdout、输入提前结束或程序退出时必须有确定结束路径，不永久阻塞。
- `pipe-visible`、`pipe-selected`、`pipe-scrollback` 采用 foot 的单向 pipe 思路：把当前可见文本、选区或回滚以 UTF-8 送到外部命令 stdin；命令 stdout/stderr 丢弃；命令内部可运行 fzf/fuzzel 等选择器并自行消费结果，不返回给调用 Termole 的 shell。
- pipe 动作通过配置的按键和命令触发；命令由 Windows shell 执行，Termole 不实现自己的命令语言。首版不默认设置 pipe 热键，避免改变普通终端行为。
- pipe 动作的临时文件和业务清理由命令负责；若命令通过 `termole -e` 打开新的交互窗口，launcher 的默认等待使命令可以在窗口关闭后再清理。Termole 只负责自己创建的管道和进程收尾。
- `pipe-command-output` 依赖 shell 的 OSC 133 命令边界，单独立项；首版不承诺。
- 不提供 `--picker`/`--picker-arg`，不内置 fzf，不实现“任意 TUI 自动返回选择结果”。fzf 是 pipe/`--stdio` 的验收例子，不是产品依赖。
- 缺失外部工具、启动失败和退出码按普通程序处理；`--stdio` 转交程序退出码，不额外分配选择器专用码。
- 不为 pipe/`--stdio` 引入 helper 可执行文件；launcher 与 GUI 角色在同一个 `termole.exe` 内，交付仍是一个 `termole.exe` + 一个 `termole.dll`。

### 兼容性与交付

- 首版验收 PowerShell、CMD、WSL、fzf、Neovim、Helix、lazygit，固定版本和用例。
- 覆盖 VT/ANSI、alternate screen、光标、Unicode 双宽/组合字符/Emoji 回退、中文 IME、鼠标、bracketed paste、滚屏和尺寸变化。
- 高级键盘等协议按实际需求逐项决定，不以“终端核心支持”替代端到端验证。
- 用户主动复制粘贴默认可用；终端输出触发的剪贴板写入默认关闭，用户/宿主可明确开启，本地和远端输出规则一致。
- 单窗口单会话，不内置标签页、分屏、全局快捷键呼出或后台常驻。独立实例互不依赖；宿主可自行组合控件。
- 便携运行包包含必要运行库和许可声明，另提供嵌入开发包与文档。终端核心不依赖后台服务，未使用的能力不额外初始化。
- 建立启动延迟、空闲 CPU/内存、峰值内存、输出吞吐、帧延迟的测量记录，优化有证据，不先承诺未测数值。

## Testing Decisions

状态：以下测试边界已获用户确认。

### 主要边界：公开宿主能力

以独立最小 C++ 宿主通过公开 C ABI / C++23 包装驱动真实 Session/View 为主要验收入口。优先检验可观察行为：真实进程收到输入、画面变化、尺寸变化、输出快照/选择、事件、退出和资源清理，而不是私有类、内存布局或解析器内部状态。

这条边界同时约束独立应用复用相同实现。只需布局测试时可使用受控子程序；进程、管道、IME 和嵌入验收必须使用真实 Windows 设施，不能以 MockTerm 或静态文字快照代替。

### 必要外部边界：CLI 进程及数据通道

CLI 的启动、配置覆盖、标准句柄、Shell 等待、退出码，以及 `--stdio`/pipe 的原始字节必须从真实调用进程检验。使用 CMD 和 PowerShell 的调用场景，覆盖无控制台/双击启动、重定向和参数转义；不通过内部配置函数的返回值推断外部行为。

### 验收场景

| 场景 | 外部可观察的通过条件 |
| --- | --- |
| 双入口最小终端 | 独立窗口和独立宿主均能通过同一组件运行 PowerShell，输入输出、调整尺寸、退出可操作 |
| 会话与视图寿命 | 销毁 View 后受控程序继续输出且不阻塞；重建 View 恢复当前画面和保留历史 |
| 进程清理 | 关闭独立窗口或销毁宿主后，本会话进程树结束；不影响其他会话 |
| 输出排空 | 子进程退出前的尾部标记可见，再交付完成事件；异常持有句柄不导致永久等待 |
| 宿主接口 | 错误参数、无效状态、版本不匹配明确失败；分配释放和线程约定有独立宿主验证 |
| 事件与销毁 | 排队事件后销毁对象、事件内请求终止、上下文销毁，不出现失效对象回调或死锁 |
| 真实 CLI | 逻辑参数、目录、环境、配置覆盖、标准句柄和退出行为与契约一致 |
| 入口与等待 | 普通模式 launcher 立即返回；`-e`/`--stdio` 等待并返回程序退出码；`--stdio` 管道阶段按顺序执行 |
| 外部程序 | `-e` 子进程 fd 0/1/2 接 PTY；`--stdio` 的 stdin/stdout 字节与退出码原样，fzf UI 可见；两者不互相冒充 |
| pipe 动作 | `pipe-visible`/`pipe-selected`/`pipe-scrollback` 收到正确 UTF-8 文本，外部命令内部选择/动作成功，调用方 stdout 不被污染 |
| 异常管道 | 调用方关闭 `--stdio` 结果管道、输入提前结束、大结果背压时可结束和清理，不永久卡死 |
| 终端兼容性 | 指定版本程序完成固定交互任务，alternate screen、光标、滚屏、鼠标和重绘行为可复现 |
| 文字与输入 | 中文 IME 候选和提交、CJK 双宽、组合字符、Emoji 回退、选择与粘贴不破坏网格 |
| 窗口与 DPI | 布局、焦点、缩放和尺寸变化在独立与嵌入模式均可用 |
| 剪贴板规则 | 主动复制粘贴可用；默认忽略输出触发写入，授权后行为符合设置 |
| 交付 | Windows 10 22H2 与 Windows 11 x64 上，便携包在无开发工具环境启动，开发包可构建独立宿主 |
| 资源与性能 | 空闲不持续重绘，滚屏和事件队列有界；记录可复现基准和测试环境 |

既有先例包括：真实 fzf 的八类 ConPTY/独立数据通道测试；原生子 HWND 与 DirectWrite/Direct2D 绘制探针；Contour 的 VT、alternate screen、字符输入和 CJK 宽度比较探针。这些是设计与回归来源，不能替代成品端到端测试。旧 picker 的临时文件结果转发只保留为历史证据，不作为 v0.3 的 `--stdio` 验收替代。

默认保持一个宿主能力边界和一个必要的 CLI 进程边界；只有 Unicode、VT 状态转换等失败需要更快定位时，增加窄范围的协议/数据用例，避免为每个内部实现层建立镜像测试。UI 可见性必须使用截图/像素证据；`screen_text`/`viewport_text`/`paint_count` 不是可见性证明。

## Out of Scope

- 非 Windows 平台、首版 ARM64 和 Windows 10 22H2 之前的系统。
- Electron、WebView、Qt 或完整跨平台 GUI 框架作为产品依赖。
- SSH 管理、文件管理、模糊查找算法、Git 客户端、Shell、IDE、工作区管理或命令面板的重实现。
- 首版 QuickJS、插件市场、自定义脚本语言/VM、通用文件或网络脚本 API。
- 多视图共享一个 Session、跨进程嵌入、跨进程 SetParent 方案、宿主退出后的会话保活。
- 标签页、分屏、全局呼出、后台常驻、daemon 及共享缓存服务。
- 内置 picker、`--picker`/`--picker-arg`、任意 TUI 自动输出结构化选择结果、修改 fzf、WSL fzf 的 picker 保证。
- `pipe-command-output` / OSC 133 命令输出导出（后续单独立项）。
- 静态 SDK、多渲染后端、所有高级终端协议一次性完整支持。
- Pip 插画、完整品牌视觉制作及未经实测的性能数值承诺。

## Further Notes

- 本规格汇总原始 v0.1、已确认追问、ADR 0001–0007（0003 已被 0006 取代）和用户对复用/选择器边界的后续纠正。冲突时以本轮确认决定为准；原始资料继续保留。
- 截至 2026-10-30，统一验证入口的默认两组测试通过；显式启用 Contour 比较探针后的三组测试也通过。尚无正式终端、DLL SDK 或发布包。
- 2026-10-31：按 foot 1.28.0 源码核查修订为 v0.3。foot `-e` 只接 PTY，不转发调用方 stdio；foot 的 pipe-* 是单向文本到外部命令。Termole 以 `-e` 对齐 execute 思路，以 `--stdio` 显式提供调用方数据通道，以 pipe-* 对齐外部命令用例；不再内置 picker。
- 2026-10-31：补充控制台 launcher / GUI 双角色和默认等待（ADR 0007）。普通模式立即返回；`-e`/`--stdio` 等待并返回退出码；仍是一个 `termole.exe` + 一个 `termole.dll`。
- 测试发生在本机 Windows x64 与已有源码缓存上；不等同于干净联网构建、Windows 10 兼容、真实 IME/DPI 或完整会话嵌入已经通过。
- 参考实现中，Ghostty 提供设计思路，Windows Terminal 提供 Windows 终端与平台实现参考，Contour 提供 C++ 终端状态与协议行为参考；它们的探针或文档不构成产品依赖批准。
- 后续任务按完整用户路径拆分并声明阻塞关系，首个切片必须同时证明独立终端和原生嵌入，不能先做一个只支持独立窗口的产品再补抽取。
