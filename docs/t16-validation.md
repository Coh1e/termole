# T16 验证：`--stdio` 的字节、退出码和大输入

- 任务：[#17](https://github.com/Coh1e/termole/issues/17)
- 决策：[ADR 0006](adr/0006-terminal-selector-boundary.md)（通用数据通道）、[ADR 0007](adr/0007-console-launcher-wait.md)（等待与退出码）
- 日期：2026-10-31
- 环境：Windows 11 x64；MSVC 14.51（VS 18 BuildTools）、CMake 4.4、Ninja；Release；Windows PowerShell 5.1；fzf 0.74.5
- 前置：T15（[#16](https://github.com/Coh1e/termole/issues/16)）已收口

## 结论

`--stdio` 作为通用外部程序数据通道通过本票验收：在真实 CMD/PowerShell 调用中结果字节一致、退出码转交，NUL、Unicode、内嵌换行、超大输入和慢读消费者不丢字节且能结束；`-e` 仍只把 PTY 交给子进程；调用方提前关闭 stdout、输入提前结束、程序启动失败和用户取消都有确定结束路径。

本票没有修改产品代码，没有修改 fzf，也没有新增结果解析。fzf 只是外部程序示例：所有通用字节与背压用例都由受控的 `cli-report-helper` 完成，fzf 只用于验收标准点名的 `--filter` 组合。

**未主张通过：** 交互式 fzf 全屏 UI。`--filter` 是纯数据路径，没有 fzf UI；全屏可见性仍由 [#26](https://github.com/Coh1e/termole/issues/26)、[#27](https://github.com/Coh1e/termole/issues/27) 和 T06（[#7](https://github.com/Coh1e/termole/issues/7)）验收，本文件不以 `screen_text`/`viewport_text` 代替截图证据。

## 本次改动

- `tests/launcher_test.cpp`：新增 2 MiB 二进制负载 + 慢读消费者、`--stdio` 用户取消、`--stdio` 缺失程序与存在但无法启动的程序；`-e` 用例增加“调用方 stdout 管道没有收到程序输出”的断言。
- `tests/run_cli_pipelines.cmake`：新增真实 CMD 的 fzf `--filter`/`--multi`/无匹配退出码、2.2 MB 管道输入。
- `tests/run_cli_pipeline.ps1`：新增 PowerShell fzf `--filter`/`--multi`/无匹配退出码、fzf `--read0 --print0`（NUL 分隔 + 内嵌换行）、真实 CMD 重定向下 128 KiB 全字节值 + 中文/Emoji 往返。
- `tests/CMakeLists.txt`：把 `fzf.exe` 探针路径传给管道测试，并提高该测试的超时。
- `docs/agents/testing.md`：在 launcher 测试家族说明中登记本票覆盖。

没有修改 `src/`、`apps/`、`include/` 或 fzf。

## 逐条验收

| 验收标准 | 证据 | 结果 |
| --- | --- | --- |
| `--stdio fzf --filter ...` 从真实 CMD/PowerShell 调用：stdout 与 fzf 结果字节一致，无绘制/诊断污染，退出码转交 | **CMD**：`type candidates \| termole --stdio fzf --filter=alp > via` 与直接 `fzf --filter=alp < candidates > direct` 对比；均为 6 B，SHA-256 `B6A98D9C…B51060`，退出 `0`，结果不含 ESC。`--multi` 变体均为 16 B，SHA-256 `EF5BB271…D8D9650`。无匹配时转交 fzf 的 `1`。**PowerShell**：`$candidates \| & termole --stdio fzf --filter=alp` 的行与直接 fzf 一致；`--multi` 一致；无匹配 `$LASTEXITCODE` 为 `1`；结果不含 ESC。 | 通过 |
| NUL 输入输出、`--multi`、内嵌换行、中文/Emoji、超大输入和慢读消费者不丢字节、不永久等待 | **NUL/Unicode/内嵌换行**：PowerShell 经真实 CMD 重定向往返 131,086 B（0x00–0xFF 全字节值 + `中文-🐭` + CRLF/LF），输入输出 SHA-256 均为 `812B6F64…7709C8D`，输出含 NUL；fzf `--read0 --print0` 的候选包含内嵌 LF 和 Emoji，直接与经 Termole 的输出均为 29 B（`alpha\nwith newline\0beta\0zeta\0`）且哈希相同。**超大输入**：C++ 侧 2,097,152 B 数据库式全字节负载逐字节相等；真实 CMD `type` 管道 2,228,224 B 输入输出 SHA-256 均为 `8F66849B…F9B434`。**慢读消费者**：消费者每 8 KiB 读一次并休眠 2 ms，2 MiB 负载仍逐字节相等，程序退出码 `5` 转交，耗时 ≥200 ms 且远小于超时。**`--multi`**：作为普通程序参数原样到达 fzf；Termole 不解析选择结果。 | 通过 |
| 调用方提前关闭 stdout、候选输入提前结束、程序启动失败和用户取消均有确定结束路径 | **提前关闭 stdout**：`t26_launcher` 的 `closed_output_pipe_ends_cleanly`（消费端在启动前已关闭，256 KiB 输入）在 15 s 内结束并转交程序状态；CMD/PowerShell 的 `--read-one`/`Select-Object -First 1` 用例均在超时内结束。**输入提前结束**：`--copy-stdin` 在 EOF 返回；fzf `--filter` 读完候选即退出；`stdio_stream_probe` 立即关闭 stdin 仍正常退出。**启动失败**：缺失程序返回 `1` 且 stderr 为 `program not found`；存在但被独占打开的程序返回 `1`、诊断含 `CreateProcessW`、不含 `program not found`，无加载器对话框。**取消**：`-e` 关闭窗口返回 `130`（既有）；新增 `--stdio` 关闭窗口返回 `130`，且调用方 stdout 未收到额外字节。 | 通过 |
| `-e` 测试证明子进程拿到的是 PTY 而不是调用方 pipe；调用方管道不被偷偷转发 | 给 launcher 一个含数据的调用方 stdin 管道和一个 stdout 管道：子程序报告 `stdin.console=1`、`stdout.console=1`、`stderr.console=1`；程序结束时调用方 stdout 管道为空（新增断言），程序的最终 marker 只进入 ConPTY。 | 通过 |
| `--stdio` 对非 fzf 的受控程序同样成立，测试不得依赖 fzf 特例 | 所有字节、NUL、大输入、慢读、提前关闭、启动失败和取消用例都使用 `cli-report-helper`；fzf 只出现在验收点名的 `--filter` 组合。 | 通过 |
| 不修改 fzf；不新增 Termole 结果解析 | 本票 diff 只有 `tests/` 与 `docs/`；`src/`、`apps/`、`include/` 未改。fzf 作为不透明程序启动，Termole 不读候选、不解析选择、不追加换行/转码/排序。 | 通过 |
| launcher 的等待/退出码在真实管道中验证；`--stdio` 在 stdin/stdout 为控制台时返回用法错误 `2` | `t26_cli_pipelines`：CMD 管道返回程序码 `6`，PowerShell 返回 `7`；fzf 无匹配返回 `1`；结果在管道表达式返回时已完整。`t26_launcher` 的 `CREATE_NEW_CONSOLE` 用例返回 `2`。 | 通过 |
| 视觉断言遵守 #27：fzf UI 必须截图/像素验证，不以 `screen_text`/`viewport_text` 代替 | `--filter` 没有 fzf UI，本票不产生视觉结论。交互式 fzf 全屏 UI 未修复（#26）且像素护栏未落地（#27），按依赖说明不宣称通过，留给 T06/#26/#27。 | 不适用（未主张 UI 通过） |

## 自动测试

`build.cmd`（configure + 全量构建 + CTest）：**31/31 通过**，78.76 s。

### `t26_launcher`（70 项断言，0 失败，17.49 s）

新增与本票相关的断言：

- `--stdio` 2 MiB 全字节负载 + 8 KiB/2 ms 慢读消费者：字节相等、退出码 `5`、实际慢读 ≥200 ms、不永久等待。
- `--stdio` 取消：窗口存在、关闭后 launcher 返回 `130`、输出流无额外字节。
- `--stdio` 缺失程序：返回 `1`，stderr 含 `program not found`，stdout 不被诊断污染。
- `--stdio` 存在但无法启动：独占打开真实程序副本，返回 `1`，stderr 含 `CreateProcessW` 且不含 `program not found`。
- `-e` 调用方 stdout 管道为空，同时子进程三个标准流仍报告 `console=1`。

### `t26_cli_pipelines`（7.51 s）

- CMD fzf `--filter` 字节对比、`--multi` 参数透传、无匹配退出码 `1`、无 ESC。
- PowerShell fzf `--filter` 行对比、`--multi` 对比、无匹配 `$LASTEXITCODE=1`。
- PowerShell fzf `--read0 --print0`：29 B 含 NUL 与内嵌 LF，直接/经 Termole 哈希相同。
- 真实 CMD 重定向：131,086 B 全字节值 + 中文/Emoji 往返，SHA-256 相同且输出含 NUL。
- 真实 CMD 管道：2,228,224 B 超大输入逐字节相等。

可复核命令：

```bat
scripts\test-one.cmd termole_launcher_test t26_launcher
scripts\test-one.cmd termole_cli_report_helper t26_cli_pipelines
```

`t26_cli_pipelines` 在 CMake 配置期探测 `fzf.exe`；未安装 fzf 时该段显式打印 SKIP，其余非 fzf 用例照常运行。测试产物（管道输入/输出、fzf 原始/经 Termole 输出）位于构建目录的 `tests/launcher-pipelines/`。

## 未覆盖/限制

- 交互式 fzf 全屏 UI、鼠标、resize 和 `--multi` 的真实多选交互仍依赖 #26/#27 与 T06/T12；本票只验证 `--filter` 数据路径，`--multi` 仅证明参数透传。
- NUL 无法经 CMD `type` 文本管道原样传输，因此 NUL 用例使用 CMD 文件重定向（仍是公开 `--stdio` 入口）；匿名管道上的 NUL 由 `t26_launcher` 直接覆盖。
- 慢读消费者在 C++ launcher harness 中精确控制读取节奏；真实 Shell 侧覆盖字节、退出码和提前关闭，未重复做慢读矩阵。
- `125`（GUI/基础设施失败）仍无故障注入，和 T15 记录一致。
- `130` 与程序自身退出码 `130` 的歧义按 ADR 0007 接受。
- 未测量数据通道吞吐；本票只验证不丢字节和可结束。
