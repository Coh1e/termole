# T26 验证：控制台 launcher / GUI 双角色与默认等待

- 任务：[#29](https://github.com/Coh1e/termole/issues/29)
- 决策：[ADR 0007](adr/0007-console-launcher-wait.md)
- 日期：2026-10-31
- 环境：Windows 11 x64 本机；MSVC 14.51（VS 18 BuildTools）、CMake 4.4、Ninja；Release

## 交付形态

- `termole.exe` 改为控制台子系统，入口 `wmain`：默认 launcher，内部 `--gui` 进入 GUI 角色；仍是 1 个 exe + 1 个 dll。
- 普通模式：launcher 启动 GUI 后返回 `0`，不等待窗口。
- `-e` / `--stdio`：launcher 等待 GUI，GUI 等待程序排空输出后退出，launcher 转交退出码。
- `--stdio`：调用方 stdin/stdout 原样接到子进程，Termole 不缓冲；子进程 stderr 合并进 ConPTY 输出流，UI 留在 Termole 窗口；Termole 自身诊断写调用方 stderr。
- 退出码：程序退出码原样；用户先关闭窗口 `130`；程序启动失败 `1`；用法错误 `2`；GUI/基础设施失败 `125`。
- 控制台：只隐藏并释放「本进程独占、为新进程分配」的控制台；共享的调用方控制台不动；GUI 不自行分配控制台（无调用方控制台时以 `CREATE_NO_WINDOW` 启动）。
- `--picker` / `--picker-arg` 及其专用代码、错误码、测试已移除。
- `termole_session_options` 新增可选 `stdin_handle` / `stdout_handle`，`TERMOLE_ABI_VERSION` 3；与 `stdin_path` / `stdout_path` 互斥。

## 自动测试

`build.cmd`（configure + 全量构建 + CTest）：**31/31 通过**（本机最终一次 80.55 s）。

### `t26_launcher`（40 项断言）

`tests/launcher_test.cpp`，启动真实 `termole.exe` 和控制台 helper：

- 普通模式：launcher 在窗口关闭前返回 `0`；GUI 进程实际运行目标程序；argv（含空格参数）保真；launcher 返回后窗口仍存在。
- 像素证据：对真实 HWND 做 `PrintWindow(..., PW_RENDERFULLCONTENT)`，1000×640 捕获有 250 种颜色、约 614k 亮像素与约 25k 暗像素（浅色系统主题），不是空白面；测试断言成功捕获、颜色数 ≥ 8、暗像素和亮像素各 > 100（背景与字形都出现）。失败时把 BMP 写到测试产物目录。
- `-e`：`--sleep-ms 1200 --exit-code 4` 实际等待 ≥ 1 s 并返回 `4`；给 launcher 一个含数据的调用方 stdin 管道，子进程仍报告 stdin/stdout/stderr 都是 ConPTY（`console=1`），调用方管道没有被透传。
- `--stdio`：经匿名管道灌入含 NUL、UTF-8 中文和约 60 KB 的负载，输出与输入逐字节相等，没有额外字节或终端转义；子进程 stdin/stdout 是调用方管道（`console=0`）；程序退出码 `9` 原样返回。
- `--stdio` 在 stdin/stdout 为控制台时返回用法错误 `2`（用 `CREATE_NEW_CONSOLE` 构造真实控制台）。
- 控制台隐藏：`CREATE_NEW_CONSOLE` 启动 `-e`，探针 `AttachConsole` 得到 `console=none` 或 `visible=0`；launcher 仍返回程序状态。该断言验证“不留下空白控制台”，不区分“先隐藏再释放”的瞬时状态。
- 控制台保留：父进程 `AllocConsole` 后启动 launcher，launcher 运行期间父控制台仍 `visible=1`；Windows Terminal 等只暴露隐藏伪窗口的环境会显式打印 SKIP。
- 取消：`-e` 运行 `--sleep-ms 8000` 时关闭窗口，launcher 返回 `130`。
- 提前关闭：调用方在启动前关闭 stdout 读端，`--stdio` 在 15 s 内结束并转交程序的 broken-pipe 状态，不永久阻塞。

### `t26_cli_pipelines`（真实 CMD/PowerShell）

`tests/run_cli_pipelines.cmake` + `run_cli_pipeline.ps1`：

- CMD：`type input.txt | termole.exe --stdio cli-report-helper.exe --copy-stdin > output.txt`，输出与输入完全一致（含中文与 CRLF），返回 `0`。
- CMD 大输入：8000 行、约 100 KB（超过管道缓冲区）走同一条 `type | termole | 文件` 路径，逐字节相等，证明流式与背压不丢字节。
- CMD 退出码：同一管道加 `--exit-code 6`，`cmd /d /c` 返回 `6`，证明默认等待让后续命令拿到真实状态。
- CMD 提前关闭：`type big | termole --stdio ... --copy-stdin | helper --read-one`，消费者读一次即退出，管道在 30 s 内结束，不永久阻塞。
- PowerShell：`@('alpha','beta') | & termole.exe --stdio ... --copy-stdin` 得到完整两行结果；`--exit-code 7` 后 `$LASTEXITCODE` 为 `7`。
- PowerShell 大输入：8000 行全部按顺序返回。
- PowerShell 提前关闭：`Select-Object -First 1` 在读到第一行后关闭结果管道，20000 行输入在 1 s 内结束且顺序保留，不永久阻塞。

## 手工核对

- `termole.exe --version` → `Termole 0.1.0`，退出 `0`；`--bad-option` → stderr 诊断，退出 `2`。
- 普通模式无参数从 PowerShell 返回用时 28 ms，退出 `0`，GUI 进程继续运行（窗口未关闭）。
- GUI 角色与调用方控制台解耦：对运行中的 GUI 进程执行 `AttachConsole` 探针返回 `console=none`，因此关闭原控制台或 Ctrl+C 不会结束终端窗口。
- `termole.exe -e cmd.exe /c "exit 7"` → `7`；`-e cmd.exe /c "echo hi > file"` 写入文件，说明 `cmd /c` 命令行未被多余引号改写。
- `"ab`ncd" | termole.exe --stdio cmd.exe /c more` → 两行原文，退出 `0`。
- `--stdio` 启动不存在的程序 → stderr `program not found`，退出 `1`。
- `--stdio` 的 stdin 为控制台（无管道）→ stderr 用法诊断，退出 `2`。

## 构建产物

- `termole.exe`：344,064 B
- `termole.dll`：327,168 B

## 未覆盖/限制

- fzf 全屏 TUI 的窗口可见性仍依赖 [#26](https://github.com/Coh1e/termole/issues/26) 与像素护栏 [#27](https://github.com/Coh1e/termole/issues/27)；本验证只证明管道字节与等待行为，不把 fzf UI 标为完整通过。
- `--stdio` 的慢读消费者、超大输入矩阵属于 T16（[#17](https://github.com/Coh1e/termole/issues/17)）；本验证覆盖 CMD 8000 行/约 100 KB、CreateProcess 管道约 60 KB 往返与 256 KB 提前关闭。
- `125` 分支未做故障注入；代码路径存在但无专门测试。
- 普通模式在 GUI `CreateProcess` 成功后即返回 `0`，之后的 GUI/Session 初始化失败不会反向改变已返回的状态；这是 ADR 0007 的约定。`-e`/`--stdio` 中若 GUI 被加载器终止（NTSTATUS 退出码），launcher 原样转交该值，不映射为 `1`/`125`。
- 控制台隐藏只直接验证“最终不留下控制台”；`ShowWindow(SW_HIDE)` 的瞬时状态未单独观测。共享控制台的可见性断言在 Windows Terminal 等只暴露隐藏伪窗口的环境会显式 SKIP。
- `130` 与程序自身退出码 130 的歧义按 ADR 0007 接受。
- `--stdio` 只接受管道或磁盘文件重定向；`NUL`（字符设备）会被判为控制台类句柄并返回 `2`。
- ABI 提升到 3 是宿主可见变更：旧宿主需用新头文件重新编译。
