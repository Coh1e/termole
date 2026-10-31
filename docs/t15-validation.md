# T15 验证：`-e` / `--stdio` / pipe 边界

- 任务：[#16](https://github.com/Coh1e/termole/issues/16)
- 决策：[ADR 0006](adr/0006-terminal-selector-boundary.md)（三条边界）、[ADR 0007](adr/0007-console-launcher-wait.md)（launcher/等待）
- 日期：2026-10-31
- 前置：T26（#29）先落地 launcher、等待与退出码；本票收口的是它与验收标准的差异

## 结论

`-e`（foot 语义，PTY-only）、`--stdio`（显式调用方数据通道）和 `pipe-*` 三条边界的**实现**由 T26（`3829672`）完成，本票做逐条核对，只补了一个缺口：**程序存在但无法启动**的诊断此前没有断言，无法证明它与「工具缺失」可区分。

## 逐条核对

| 验收标准 | 证据 | 结果 |
| --- | --- | --- |
| `-e <program> [args...]` / `--execute` 按 foot 语义运行，子进程 fd 0/1/2 接 ConPTY，不透传调用方 stdio，退出码即 Termole 退出状态 | `t26_launcher` 的 `-e` 用例：含数据的调用方 stdin 管道未被转发，子程序报告 `stdin.console=1`、`stdout.console=1`、`stderr.console=1`；`--sleep-ms 1200 --exit-code 4` 实测等待 ≥1 s 并返回 `4`；`apps/common/cli.cpp` 中 `-e`/`--execute` 作为程序分隔符 | 通过 |
| `--` 分隔符保留，`-e` 作为等价别名；参数、cwd、env 不被改写 | `t02_cli_args` 逐项核对 argv/cwd/env；普通模式下 launcher 重新引号后 GUI 解析回同一 token（`apps/standalone/main.cpp` 的 `quote_argument`） | 通过 |
| `--stdio` 把调用方 stdin/stdout 接程序，stderr/UI 走 ConPTY；任意程序成立；不读候选、不解析结果、不加换行/转码/排序 | `t26_launcher` 的 `--stdio` 用例：经匿名管道灌入 NUL、UTF-8 中文与约 60 KB 负载，输出逐字节相等；子程序报告 `stdin.console=0`、`stdout.console=0`；`t26_cli_pipelines` 用 `cli-report-helper`（非 fzf）证明不依赖选择器 | 通过 |
| `--stdio` 要求 stdin/stdout 是管道或文件，控制台时返回用法错误 `2`；`-e` 不要求重定向 | `t26_launcher`：`CREATE_NEW_CONSOLE` 构造真实控制台后返回 `2`；`-e` 在新控制台与共享控制台下均正常 | 通过 |
| `--stdio` 转交子进程退出码；调用方关闭 stdout 或输入提前结束时干净退出 | `t26_launcher` 返回程序码 `9`；`closed_output_pipe_ends_cleanly`（256 KB 输入、消费端已关闭）与 `t26_cli_pipelines` 的 CMD/PowerShell 提前关闭用例均在超时内结束 | 通过 |
| `-e`/`--stdio` 由 launcher 默认等待，普通模式不等待 | `t26_launcher`：普通模式 28 ms 返回 `0` 且窗口仍存在；`-e` 等待并返回程序码 | 通过 |
| 失败诊断区分「程序启动失败 / 工具缺失 / 调用方管道错误」，不分配 picker 专用错误码 | 本次新增 `t26_launcher` 的 `start_failure_is_distinguishable`：独占打开的程序让 `CreateProcessW` 失败，stderr 含 `CreateProcessW` 且不含 `program not found`，退出 `1`；`t02_cli_error` 覆盖缺失程序（`program not found`，退出 `1`）与非法 cwd；`apps/common/exit_codes.h` 只有 `1`/`2`/`125`/`130` | 通过（本次补齐） |
| 不新增 `--picker`/`--picker-arg`/helper exe/fzf 分支；一个 exe + 一个 dll | `3829672` 删除了 picker 专用代码、错误码与测试；`apps/CMakeLists.txt` 只导出 `termole.exe`；发布包仍是 `termole.exe` + `termole.dll` | 通过 |
| CMD/PowerShell 真实调用验证 `-e` 不偷管道、`--stdio` 字节与退出码 | `t26_cli_pipelines`：`type input.txt \| termole --stdio ... > output.txt` 逐字节相等；8000 行约 100 KB 背压不丢字节；`cmd /d /c` 返回 `6`，PowerShell `$LASTEXITCODE` 为 `7`；`-e` 的调用方管道未被转发 | 通过 |
| fzf UI 可见性依赖 #26/#27，未修复前不得标为完整通过 | `t26_validation.md` 与本文件都不主张 fzf 全屏 UI 通过；该验收留在 T06（#7）与 #26/#27 | 未主张通过（受限于 #26/#27） |

## 本次改动

- `tests/launcher_test.cpp`：新增 `start_failure_is_distinguishable`，放在 launcher 测试家族内（`docs/agents/testing.md` 规定 CLI 契约只有一个家族）。

### 一个反例，留给后来者

最初用「文本文件改名 `.exe`」制造启动失败：`CreateProcessW` 会**返回成功**，由 Windows 加载器在子进程里弹出「不支持的 16 位应用程序」模态框，进程挂到用户点确定为止——测试因此超时，桌面上还会留下一个错误框。

改成**独占打开一个真实副本**：`CreateFileW(GENERIC_READ, share=0)` 持有句柄时 `CreateProcessW` 干净地返回 `ERROR_SHARING_VIOLATION`，错误消息为 `CreateProcessW: 另一个程序正在使用此文件…`，没有加载器 UI，测试 10 s 内结束。

## 未覆盖

- `125`（GUI/基础设施失败）分支仍无故障注入测试；代码路径存在，验证靠阅读而非断言。
- fzf 全屏 UI 的可见性、鼠标与 resize 端到端仍待 #26/#27 与 T06/T12。
- 完整的大输入/慢读矩阵属于 T16（#17）；本票只核对边界，不建立完整矩阵。
