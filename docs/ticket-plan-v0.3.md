# Termole 首版任务拆分 v0.3

来源：[实施规格 #1](https://github.com/Coh1e/termole/issues/1)（v0.3）。本文件只列出 v0.3 相对 v0.2 发生变化的外部程序/入口切片；T01–T14、T17–T24 的任务正文仍以 [v0.2 任务拆分](ticket-plan-v0.2.md) 和对应 GitHub Issue 为准。T15、T16、T25、T26 以本文和 GitHub 为准。

每项完成应可单独演示或通过公开宿主/真实 CLI 边界验收。v0.3 的三条边界不能混成一个 picker：

- `-e`：foot 语义，子进程 fd 0/1/2 接 ConPTY，不转发调用方 stdio。
- `--stdio`：Termole 显式扩展，调用方 stdin/stdout 接程序，UI/stderr 走 ConPTY。
- `pipe-*`：终端文本到外部命令 stdin 的单向动作，结果不返回调用方。

## T15：实现 foot 语义的 -e 与显式 --stdio 边界

**Blocked by:** T02、T03、T26。

**What it delivers:** 用户能在终端中用 `-e` 运行程序并把 PTY 交给它；需要调用方管道时用显式 `--stdio`，Termole 不解析结果。两者都通过控制台 launcher 提供确定的退出状态。

**Acceptance criteria:**

- [ ] `-e <program> [args...]`（及 `--execute`）按 foot execute 语义运行：子进程 fd 0/1/2 都接 ConPTY；不把调用方 stdin/stdout 透传；退出码作为 Termole 退出状态。
- [ ] 现有 `--` 目标分隔符保留；`-e` 作为 foot 兼容别名；参数、cwd、env 不被改写。
- [ ] `--stdio <program> [args...]` 把调用方 stdin/stdout 接到程序，程序 stderr/UI 走 ConPTY；对任意程序成立，不要求 fzf；Termole 不读候选、不解析结果、不追加换行/转码/排序。
- [ ] `--stdio` 转交子进程退出码；调用方关闭 stdout 或输入提前结束时干净退出，不永久阻塞。
- [ ] 失败诊断区分“程序启动失败/工具缺失/调用方管道错误”；不分配 picker 专用错误码。
- [ ] 不新增 `--picker`、`--picker-arg`、helper exe 或 fzf 专用分支；launcher 与 GUI 角色仍在同一个 `termole.exe` 内，交付一个 exe + 一个 dll。
- [ ] CMD/PowerShell 真实调用验证 `-e` 不偷管道，`--stdio` 的字节与退出码正确；fzf 只作为外部程序示例。
- [ ] fzf UI 可见性依赖 #26 与像素护栏 #27；未修复前不得把 `--stdio` fzf 标记为完整通过。

**Spec coverage:** v0.3 外部程序与数据通道；user stories 30–37、60。

## T16：验证 --stdio 的字节、退出码和大输入

**Blocked by:** T15。

**What it delivers:** `--stdio` 作为通用外部程序数据通道，在真实 CMD/PowerShell 管道中保持字节、退出码、取消和大输入行为；不把 fzf 语义写进 Termole。

**Acceptance criteria:**

- [ ] `--stdio fzf --filter ...` 从真实 CMD/PowerShell 调用：stdout 与 fzf 结果字节一致，无绘制/诊断污染，退出码转交。
- [ ] NUL 输入输出、`--multi`、内嵌换行、中文/Emoji、超大输入和慢读消费者不丢字节、不永久等待。
- [ ] 调用方提前关闭 stdout、候选输入提前结束、程序启动失败和用户取消均有确定结束路径。
- [ ] `-e` 测试证明子进程拿到的是 PTY 而不是调用方 pipe；调用方管道不被偷偷转发。
- [ ] `--stdio` 对非 fzf 的受控程序同样成立，测试不得依赖 fzf 特例。
- [ ] 不修改 fzf；不新增 Termole 结果解析。
- [ ] 视觉断言遵守 #27：fzf UI 必须截图/像素验证，不以 `screen_text`/`viewport_text` 代替。

**Spec coverage:** v0.3 user stories 31、33–36；Testing Decisions 外部程序/异常管道。

## T25：实现 foot 风格 pipe-visible / pipe-selected / pipe-scrollback

**Blocked by:** T11、T14、T16、T26。

**What it delivers:** 把当前可见文本、选区或回滚送到外部命令 stdin；外部命令拥有自己的 UI/选择语义，stdout/stderr 丢弃；Termole 不把结果返回调用方。

**Acceptance criteria:**

- [ ] `pipe-visible`：发送当前 View 可见文本（UTF-8）；`pipe-selected`：发送当前选区；`pipe-scrollback`：发送有界回滚文本。
- [ ] 命令通过配置的按键触发；命令字符串由 Windows shell 执行，Termole 不实现自己的命令语言；首版不默认设置热键。
- [ ] 外部命令 stdin 收到准确文本；stdout/stderr 不进入 Termole 终端、不污染调用方 stdout；命令退出码不影响 Termole 会话，除非命令启动失败需要诊断。
- [ ] 命令内部可以运行 fzf/fuzzel 等选择器并执行动作；测试只验证“文本已送达、命令已运行”，不实现结果返回协议。
- [ ] View、Session、独立入口/嵌入入口行为一致；选择/回滚的边界与当前 API 定义一致。
- [ ] 配置解析、按键冲突/非法命令有明确诊断；无配置时不注册默认 pipe 热键。
- [ ] 命令自己的临时文件和业务清理由命令负责；若命令通过 `termole -e` 打开新的交互窗口，T26 的默认等待使命令可以在窗口关闭后再清理。
- [ ] 不新增 helper exe、不修改 fzf；仍是一个 `termole.exe` + 一个 `termole.dll`。

**Out of scope:** `pipe-command-output`（OSC 133）；把外部命令的选择结果返回 Termole 调用方。

**Spec coverage:** v0.3 外部程序与数据通道；user stories 32、35–37。

## T26：控制台 launcher / GUI 双角色与默认等待

**Blocked by:** T02、T03。

**状态：已实现并通过本机验证，见 [T26 验证](t26-validation.md)。** 任务关闭状态以 GitHub 为准。

**What it delivers:** 普通模式不阻塞调用 shell；`-e`/`--stdio` 默认等待程序结束并返回退出码；仍是一个 `termole.exe` + 一个 `termole.dll`。

**Acceptance criteria:**

- [ ] `termole.exe` 改为控制台子系统；默认角色是 launcher，内部 `--gui` 角色创建窗口、Session 和 View。
- [ ] 普通模式 launcher 成功启动 GUI 后返回 `0`，不等待窗口关闭；启动失败返回 `1`。
- [ ] `-e`/`--stdio` launcher 继承调用方标准句柄，启动 GUI 角色并等待；返回程序退出码、用户取消 `130`、基础设施 `125`。
- [ ] `--stdio` 的 stdin/stdout 直接接到 GUI/子进程；字节、背压、提前关闭在真实 CMD/PowerShell 管道中验证。
- [ ] `--stdio` 在 stdin/stdout 为控制台时返回用法错误 `2`；`-e` 不要求重定向。
- [ ] launcher 隐藏自己新分配的控制台，不隐藏调用方控制台；从 Explorer 启动不留下空白控制台。
- [ ] 仍只有 `termole.exe` + `termole.dll`；不新增 helper 或第二个 exe。
- [ ] 测试覆盖普通模式立即返回、`-e`/`--stdio` 等待与退出码、管道顺序、控制台隐藏；不修改 fzf。
- [ ] 文档更新 CLI、README、ADR 0007；UI 证据遵守 #26/#27。

**Spec coverage:** v0.3 user story 60；Testing Decisions 入口与等待。

## 执行顺序

1. T26（launcher/等待）先落地；否则 `--stdio` 在 CMD/PowerShell 中无法可靠顺序执行。
2. T15（`-e`/`--stdio` 边界）依赖 T26。
3. T16（字节/背压）与 T25（pipe 动作）可以并行，均依赖 T15。
4. UI 结论并行推进 #27（像素护栏）→ #26（fzf 空白）→ T06；未修复前 T15/T16 的 fzf UI 只算部分通过。
5. T20/T22/T23/T24 在 T16/T25/T26 之后更新资源、ABI、打包和验收。

## 发布约定

T25、T26 已作为规格 #1 的原生子任务发布，标记 `ready-for-agent`；T15/T16 在 v0.3 同步后恢复 `ready-for-agent`。所有前置任务关闭后才可开始。`ready-for-agent` 表示内容可执行，不表示可以忽略阻塞关系。
