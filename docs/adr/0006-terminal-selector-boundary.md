# ADR 0006：终端不内置选择器，区分 execute、stdio 与 pipe

- 状态：accepted
- 日期：2026-10-31
- 取代：ADR 0003（选择器结果使用独立数据通道）

## 背景

原始 picker 设计把“候选 stdin、结果 stdout、fzf UI”做成 Termole 的 `--picker` 模式。用户随后明确：fzf 只是外部候选选择器；Termole 不应理解选择语义，也不应把 fzf 内置成产品能力。

对 foot 1.28.0 的源码核查（见 [foot-stdio-research](../foot-stdio-research.md)）表明：

- `foot -e` 只把子进程 fd 0/1/2 接到 PTY，**不转发**调用方 stdin/stdout；
- foot 的 `pipe-visible` / `pipe-selected` / `pipe-scrollback` / `pipe-command-output` 把终端文本送到外部命令 stdin，命令 stdout/stderr 丢弃；
- `pipe-*` 可以运行 fzf/fuzzel 等选择器，但结果由命令自己消费，不返回调用方；
- `footclient` 只转发退出码，不转发 stdout。

因此，不能把 `pass ls | foot -e fzf` 当作 foot 的既有行为；Termole 也不应把它伪装成同一行为。

## 决策

Termole 采用“终端与外部程序分离”的三层边界：

1. **`-e <program>` = foot 语义**
   - 程序在 ConPTY 中运行，fd 0/1/2 都接终端；
   - 不把调用方 stdin/stdout 透传给子进程；
   - 不解析程序输出。

2. **`--stdio <program>` = Termole 显式扩展**
   - 调用方 stdin/stdout 接到程序，stderr/UI 走 ConPTY；
   - 对任意程序成立，不要求 fzf；
   - Termole 只转发字节和退出码，不追加换行/转码/排序；
   - 这是显式声明的 Termole 行为，不声称与 foot `-e` 等价。

3. **`pipe-visible` / `pipe-selected` / `pipe-scrollback` = foot 风格单向动作**
   - 终端文本以 UTF-8 送到外部命令 stdin；
   - 命令 stdout/stderr 丢弃；
   - 命令内部可使用 fzf/fuzzel 等选择器并执行动作；
   - 不提供调用方结果返回协议。

不提供 `--picker` / `--picker-arg`，不内置 fzf，不实现任意 TUI 的自动结果返回。`pipe-command-output` 依赖 shell OSC 133，单独立项。

交付仍是一个 `termole.exe` + 一个 `termole.dll`。`termole.exe` 的控制台 launcher / GUI 双角色及默认等待见 [ADR 0007](0007-console-launcher-wait.md)。

## 备选方案

- **保留 `--picker`**：拒绝。它把 fzf 语义写进终端，证据和测试都会围绕 fzf 而不是通用终端；用户明确反对。
- **只做严格 foot 语义**：拒绝。`-e` 无法满足“调用方管道 → UI → 调用方结果”的需求；因此保留一个显式的 `--stdio` 扩展，并清楚标注差异。
- **只靠 shell 临时文件/FD 包装**：拒绝为唯一方案。它可用，但 Windows 没有 fd 3/4 的直接对应；Termole 提供通用 `--stdio`，shell 包装仍可作为外部组合。

## 后果

- 需要同时维护 `-e` 与 `--stdio` 两条 CLI 路径，并在帮助/文档/测试中明确区分。
- `--stdio` 的 fzf UI 可见性依赖 View 全屏 TUI 渲染修复（#26）与像素护栏（#27）。
- pipe 动作需要可配置 key binding 和命令执行；默认不注册热键，避免改变普通终端行为。
- 不再需要 picker helper、候选/结果临时文件协议或 fzf 专用分支。
- 以后若要支持“调用方拿回选择结果”，应作为独立的 launcher 设计，而不是重新把 picker 塞回终端。

## 实现记录（2026-11-01）

T25 按本 ADR 落地 pipe 动作，三层边界不变：

- pipe 绑定来自配置文件，是独立入口的应用层能力；嵌入宿主按既有约定不读取用户配置，因此不注册 pipe 热键，仍用公开查询（视口、选区、回滚）或自己的数据通道组合命令。未新增 C ABI。
- 三个文本来源直接复用 View 视口、View 选区和 Session 回滚的现有定义；命令 stdout/stderr 指向 `NUL`，退出码不进入会话，不新增结果返回协议。
- 命令由 `%COMSPEC%`（缺省 `cmd.exe`）以 `/d /s /c "<command>"` 执行；按键在独立入口拦截。交付仍是 1 个 `termole.exe` + 1 个 `termole.dll`。
- 验证见 [T25 验证](../t25-validation.md)。

## 参考

- [foot 源码核查](../foot-stdio-research.md)
- foot `-e` / `pipe-*` 文档与源码（固定提交 `152487e494993ad7fb5633a27c7d7ad0f8810ea5`）
- 原 ADR 0003（历史，已取代）
