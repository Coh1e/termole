# ADR 0007：控制台 launcher 与 GUI 双角色提供默认等待

- 状态：accepted
- 日期：2026-10-31
- 相关：ADR 0006（execute/stdio/pipe 边界）

## 背景

`termole.exe` 当前是 WIN32 子系统程序。CMD 和 PowerShell 不会像等待控制台程序那样等待 GUI 程序；现有文档只能用 `start /wait` 或 `Start-Process -Wait` 让调用方等待。对于 `-e` 和 `--stdio`，这会让脚本无法可靠地顺序执行：

```powershell
$result = Get-Content names.txt | termole --stdio fzf
# 如果 termole 尚未退出，$result 可能为空
```

foot 用 `footclient` 解决同一问题：客户端等待终端结束，再把退出状态返回调用方。Termole 需要同等能力，但 v0.3 的交付约束仍是 `termole.exe` + `termole.dll` 两个文件，不增加第二个可执行文件。

## 决策

`termole.exe` 改为控制台子系统程序，同一个二进制承担两个内部角色：

1. **launcher（默认角色）**
   - 解析最小命令行信息，处理 `--help` / `--version`。
   - 普通模式：以 GUI 角色启动同一 exe 后立即返回 `0`；调用 shell 不被阻塞。
   - `-e` / `--stdio`：以 GUI 角色启动同一 exe，继承调用方标准句柄，等待其结束并返回退出码。
   - 如果控制台是新分配的（例如从 Explorer 启动），立即隐藏；如果附着在调用方控制台，不隐藏调用方控制台。

2. **GUI 角色（内部 `--gui`）**
   - 创建顶层窗口、Session 和 View。
   - 普通模式与 `-e`：子进程 fd 0/1/2 接 ConPTY，符合 ADR 0006。
   - `--stdio`：子进程 stdin/stdout 使用 launcher 继承的调用方句柄；子进程 stderr/UI 走 ConPTY；Termole 自身诊断走调用方 stderr。
   - 程序结束或用户关闭窗口后，GUI 角色以程序退出码、取消码或基础设施错误码退出。

## 等待与退出状态

- 普通模式：launcher 在成功启动 GUI 后返回 `0`；GUI 的退出码不反向传给已返回的 shell。需要退出码时使用 `-e` 或 `--stdio`。
- `-e` / `--stdio`：launcher 等待 GUI 角色；GUI 角色等待程序排空输出后退出。
- 程序正常或失败退出：返回程序退出码。
- 用户在程序退出前关闭窗口：终止本会话进程树，返回 `130`（Termole 取消码，可能与程序自身使用的 130 冲突）。
- 程序启动失败：`1`；用法错误：`2`；Termole 基础设施失败：`125`。
- 不引入 picker 专用错误码。

## `--stdio` 的前置条件

- `--stdio` 要求调用方 stdin/stdout 是管道或文件重定向；stdin/stdout 是控制台时返回用法错误 `2`，避免把键盘输入当成候选、把结果写回终端。
- `--stdio` 不解析结果、不追加换行、不转码、不排序；字节和背压由继承的句柄直接传递。
- 子进程 stderr/UI 仍在 ConPTY；Termole 自身诊断写调用方 stderr。程序若把 UI 写到 stderr，不属于 `--stdio` 支持范围。

## 备选方案

- **两个可执行文件（控制台客户端 + GUI 宿主）**：最直接，但把发布包从 1 exe 变成 2 exe，和 v0.3 的交付约束冲突。若 launcher 方案在实现中被证明不可靠，再单独决定是否改成两个 exe。
- **保持 GUI 异步，只文档要求 `start /wait`**：拒绝。它不能满足脚本顺序执行，也不是 footclient 式默认等待。
- **单角色始终等待**：拒绝。普通模式会阻塞调用 shell 直到窗口关闭，改变交互体验。
- **新增 `termolew.exe` 仅用于等待**：与两个 exe 方案相同，暂不采用。

## 后果

- `termole.exe` 的子系统、入口点和启动流程需要改造；普通模式多一次 launcher → GUI 进程启动。
- 从 Explorer 启动时可能出现短暂控制台窗口；launcher 必须尽早隐藏自己新分配的控制台，且不能隐藏调用方控制台。
- `--stdio` 的字节、背压、提前关闭和退出码必须在真实 CMD/PowerShell 管道中验证，不能只用 `CreateProcess` 测试代替。
- 仍需保持一个 `termole.exe` + 一个 `termole.dll`；不新增 helper 可执行文件。
- 像素/可见性结论仍按 #26/#27 的截图证据规则，不能用 `screen_text` 代替。

## 实现记录（2026-10-31）

按本 ADR 落地的具体形态：

- `termole.exe` 改为控制台子系统，入口为 `wmain`；默认角色是 launcher。GUI 角色由内部 `--gui` 作为首 token 进入，用户参数原样重引用后转交，`--gui` 不出现在公开帮助中。
- launcher 启动时用 `GetConsoleProcessList` 判断本进程是否是控制台的唯一客户端（这是“控制台为新启动分配”的实用判据）：是则 `ShowWindow(SW_HIDE)` 隐藏，随后 `FreeConsole` 让新控制台立即消失；共享的控制台完全不动。隐藏发生在参数校验之后，因此 `--help` 和用法错误仍可读。
- GUI 角色在入口先 `FreeConsole`，并始终由 launcher 以 `CREATE_NO_WINDOW` 启动，因此不绑定任何调用方控制台：关闭调用方控制台窗口或按 Ctrl+C 不会结束终端。需要写诊断时临时 `AttachConsole(ATTACH_PARENT_PROCESS)`，写完立即 `FreeConsole`。
- 普通模式：launcher 在 `CreateProcess` 成功后立即返回 `0`。`-e` / `--stdio`：launcher 用 `STARTF_USESTDHANDLES` 把调用方的**重定向**标准句柄（管道/文件；控制台句柄跳过）交给 GUI，`WaitForSingleObject` 后转交 GUI 退出码。
- 退出码边界：launcher 无法启动 GUI 角色属于入口启动失败，返回 `1`（与 #29 的普通模式启动失败一致）；GUI 内部的窗口/View 等基础设施失败返回 `125`；子程序启动失败返回 `1`。
- `--stdio`：GUI 把继承来的 stdin/stdout 句柄原样交给子进程；子进程 stderr 接到 ConPTY 的输出管道，UI 仍在 Termole 窗口中。Termole 自身诊断仍写调用方 stderr。数据通道不经过 Termole 缓冲。
- 会话退出码约定：程序退出码原样转交；用户先关闭窗口为 `130`；程序启动失败 `1`；用法错误 `2`；GUI/基础设施失败 `125`。`-e` / `--stdio` 忽略保留窗口策略，程序排空输出后关窗退出。
- 为把调用方标准句柄直接接到子进程，`termole_session_options` 在末尾增加可选的 `stdin_handle` / `stdout_handle`，`TERMOLE_ABI_VERSION` 提升到 3；两者与原有的 `stdin_path` / `stdout_path` 互斥。
- 移除了 `--picker` / `--picker-arg` 及 picker 专用代码和测试，不保留 picker 错误码。
- 顺带修正了子进程参数引用：只有含空格、制表符、引号或为空的参数才加引号，避免 `cmd /c ...` 这类自行解析命令行的程序因多余引号改变语义。

验证与证据见 [T26 验证](t26-validation.md)：`t26_launcher` 覆盖普通模式立即返回、`-e`/`--stdio` 等待与退出码、数据通道字节、取消 `130`、控制台隐藏/保留；`t26_cli_pipelines` 在真实 CMD 和 PowerShell 管道中验证字节与退出状态；另有 `PrintWindow` 像素证据确认窗口不是空面。
