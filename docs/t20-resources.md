# T20 资源有界验证

- 任务：[#21](https://github.com/Coh1e/termole/issues/21)
- 父规格：[#1](https://github.com/Coh1e/termole/issues/1)（20、23–24、37、41–43、49 稳定性）
- 相关决策：[ADR 0006](adr/0006-terminal-selector-boundary.md)（`-e` / `--stdio` / pipe 边界）、[ADR 0007](adr/0007-console-launcher-wait.md)（launcher 等待与退出状态）
- 日期：2026-11-01
- 环境：Windows 11 x64；MSVC 14.51（VS 18 BuildTools）、CMake 4.4、Ninja；Release
- 起点：`6317b4a`；本次改动后 `build.cmd` **40/40 CTest 通过**（236.50 s）

## 结论

持续输出、标题/响铃洪泛、多 Session 反复创建销毁、宿主销毁和 CLI 等待路径都保持有界：历史仍为 1000 行，事件队列仍为 256 条且退出事件不被洪泛挤掉，句柄与 working set 不随重复操作增长，取消/启动失败/用户关闭/宿主销毁都在明确截止内结束。

本次发现并修复了一处真实泄漏：当程序映像存在但不可读（例如被另一个进程独占打开）时，`CreateProcessW` 会在失败路径上让宿主进程累积 Windows 内部句柄。修复前 `t20_soak` 的 20 次锁定程序失败从 88 个句柄涨到 135 个；独立探针从 96 涨到 138。现在在创建管道和伪控制台之前用加载器的共享模式预检程序映像，把这一类失败挡在资源分配之前；预热后句柄不再增长（本机 20 次失败：84→84→94→93，预热后的 15 次为 94→93）。

没有发现数据通道、Session 读取线程或 View watcher 的持续泄漏或永久阻塞；本票没有增加常驻服务、后台守护进程或新的 ABI。

## 实现上限

- 回滚历史：1000 行（`TERMOLE_SCROLLBACK_LINES`），超出丢弃最旧行。
- 事件队列：256 条；相邻标题事件合并，退出/错误事件在洪泛时优先保留；OSC 负载最多 512 个码点，CSI 参数最多 32 个，单元格组合字符最多 16 个。
- 每个 Session 的进程树放入 Job Object（`KILL_ON_JOB_CLOSE`）；View 销毁不终止 Session，Session 销毁/terminate 清理进程树。
- 无 View 时 reader 仍消费输出，仅保留有界终端状态；不缓存完整原始输出流。
- Session 的 I/O 线程不调用宿主回调；事件由宿主主动 poll。
- `--stdio` 把调用方标准句柄直接交给子进程，Termole 不缓冲数据通道；因此该路径没有内部队列随输入增长的问题。

## 本次改动

- `src/session/session.cpp`：新增 `check_program_readable`。在程序解析、工作目录和环境保护校验之后、创建管道/伪控制台之前，以 `GENERIC_READ` + `FILE_SHARE_READ | FILE_SHARE_DELETE` 打开程序映像并立即关闭；失败时返回 `TERMOLE_STATUS_SYSTEM_ERROR`，诊断以 `CreateProcessW preflight:` 开头。程序缺失仍返回 `program not found` 和 `TERMOLE_STATUS_NOT_FOUND`，启动失败仍映射为退出码 `1`。
- `tests/soak_test.cpp`：新增 20,000 次标题/响铃交替洪泛、24 次顺序 Session 创建/运行/销毁、8 个并发 Session 的创建/提前销毁、20 次缺失程序 + 20 次锁定程序创建失败；保留 3×200,000 行无 View 输出和 working-set 上限。
- `tests/t03_t05_test.cpp`：新增宿主销毁清理用例——运行中直接销毁 Session（无 View）、View 持有 Session 时先销毁宿主 Session、运行中创建/销毁 View；断言 5 s 截止、子进程消失、句柄趋势有界，并确认 View 销毁后原 HWND 不再是窗口。
- `tests/launcher_test.cpp`：`--stdio` 2 MiB 慢读消费者、调用方提前关闭 stdout、`-e`/`--stdio` 取消用例现在记录子进程 PID，并在 launcher 退出后断言子进程已消失；取消用例还测量 15 s 截止，慢读用例打印实测字节数和耗时。
- `tests/test_process.h`：宿主与 launcher 测试共用的 PID 文件解析、进程存活/退出等待和句柄计数辅助；使用公开 Win32 API，不依赖 Termole 内部实现。
- `tests/cli_report_helper.cpp`：新增 `--write-pid <path>`，让测试从公开进程边界观察真实子进程。
- `tests/CMakeLists.txt` 未改；没有新增 CTest 条目、常驻服务或安装/打包项。

## 自动验证

本次改动后的 `build.cmd`（configure + 全量构建 + CTest）：**40/40 通过**，236.50 s。

### `t20_soak`（17.02 s）

| 场景 | 负载 | 实测 | 断言 |
| --- | --- | --- | --- |
| 无 View 持续输出 | 3 × 200,000 行，80×24 | working set 6,696,960 → 峰值 11,862,016 B，增长 5,165,056 B（约 4.9 MiB） | 每次历史 ≤1000 行；增长 ≤64 MiB |
| 标题/响铃交替洪泛 | 20,000 次 OSC 标题 + BEL，无 View | 事件 256 条；耗时 333 ms；working set 增长 155,648 B（约 152 KiB） | 队列 ≤256；退出事件存在且退出码 0；最后标题 `Flood 19999` 和最新标题事件都保留 |
| 多 Session 创建/运行/销毁 | 24 轮，每轮 200 行 | 句柄 84→84；working set 增长 0 B | 24 轮全部完成；每轮历史 ≤1000、退出码 0；句柄 ≤基线+4 |
| 并发 Session 创建/销毁 | 8 个同时创建，4 个在延迟输出中提前销毁 | 句柄 84→84；working set 增长 73,728 B（约 72 KiB） | 8 个全部创建；存活会话历史 ≤1000；句柄 ≤基线+4 |
| 启动失败 | 20 次缺失程序 + 20 次被独占打开的现有程序 | 句柄 84→84（缺失阶段）→94（锁定阶段预热）→93（其余 15 次） | 40 次全部失败；预热后句柄 ≤预热值+2 |

### `t03_t05_session`（16.07 s）

新增用例的实际测量：

| 场景 | 轮数 | 最慢清理 | 句柄 |
| --- | --- | --- | --- |
| 运行中销毁 Session（无 View） | 4 | 4 ms | 126→126 |
| View 持有 Session 时先销毁宿主 Session，再销毁 View | 4 | 5 ms | 791→791 |
| 运行中创建/销毁 View 与 Session（`--flood` 背景输出） | 4 | 1 ms | 791→792 |

每次清理都断言在 5 s 内完成，并用 `--write-pid` 观察到的真实子进程在截止内消失；View 持有 Session 时先销毁宿主 Session，子进程仍存活，直到 View 销毁才清理。View 销毁后还断言原 HWND 已不是窗口，避免 watcher 继续向已销毁窗口投递消息。

### `t26_launcher`（14.03 s）

- `--stdio` 2 MiB 全字节负载 + 8 KiB/2 ms 慢读消费者：**2,097,152 B** 逐字节相等，耗时 **3,651 ms**，程序退出码 `5` 转交，launcher 退出后子进程 PID 已消失。
- 调用方提前关闭 stdout：launcher 在 15 s 内结束并转交 broken-pipe 状态；launcher 退出后子进程 PID 已消失。
- `--stdio` 取消：窗口关闭后 15 s 内返回 `130`，子进程 PID 在 5 s 内消失，输出流没有额外字节。
- `-e` 取消：窗口关闭后 15 s 内返回 `130`，子进程 PID 在 5 s 内消失。
- `--stdio` 缺失程序/锁定程序返回 `1`、诊断不污染 stdout：既有断言保持通过。
- `-e` 等待程序退出码：既有断言保持通过。

### `t26_cli_pipelines`（9.41 s）

真实 CMD/PowerShell 管道继续覆盖字节、顺序、退出码、慢读背压和调用方提前关闭；本票没有重复 T16 的字节/退出码矩阵，只在这些公开入口上保留既有回归。

### 既有覆盖

- `t03_t05_session`：1100 行输出精确保留 1000 行；400 次标题/响铃洪泛后事件队列 ≤256 且退出事件存在；View 销毁后会话继续并重附加。
- `t26_launcher` / `t26_cli_pipelines`：`--stdio` 慢读、提前关闭、取消、启动失败；真实 CMD/PowerShell 管道。
- 历史 picker 的提前关闭/慢读测试已被 ADR 0006 移除；当前等价入口是公开的 `--stdio`。

## 逐条验收

| 验收点 | 证据 | 结果 |
| --- | --- | --- |
| 覆盖无 View 持续输出、标题/响铃洪泛、多会话创建销毁、会话重附加和 picker 管道断开 | `t20_soak` 持续输出/洪泛/24 轮顺序 Session/8 个并发 Session；`t03_t05_session` 5 个既有并发 Session、View 重附加、宿主销毁；`--stdio` 提前关闭由 `t26_launcher`/`t26_cli_pipelines` 覆盖；picker 已由 ADR 0006 移除 | 通过 |
| 历史与事件队列遵守上限；退出/错误事件不会因洪泛永久丢失 | 1000 行历史在 `t03_t05_session` 和 `t20_soak` 都成立；20,000 次交替洪泛后队列 256 条、退出事件存在、最后标题事件保留；`push_event` 对退出/错误事件优先保留 | 通过 |
| 取消、启动失败、用户关闭及宿主销毁在明确截止内清理，无失效回调 | `t26_launcher` 取消返回 15 s 内、子进程 PID 消失；启动失败/用户关闭沿用既有断言；`t03_t05_session` 宿主销毁 3 个用例最慢 5 ms，View 销毁后原 HWND 不再是窗口 | 通过 |
| 从公开宿主/CLI 边界检查进程及资源趋势，修复实际泄漏或阻塞，不增加常驻服务 | 宿主侧 `t20_soak`/`t03_t05_session` 句柄与 working set；CLI 侧 `t26_launcher` 子进程 PID 消失；修复程序映像预检泄漏；无新服务或后台进程 | 通过 |

## 未覆盖/限制

- **picker 不再是产品入口**：ADR 0006 删除了 `--picker` 及其测试；本票用公开 `--stdio` 的提前关闭作为“管道断开”的等价路径，不重新引入 picker 语义。
- **不重复 T16 矩阵**：`--stdio` 的字节、顺序、退出码、超大输入和慢读背压归 T16/T26；本票只在同一组入口上增加“子进程在 launcher 退出后消失”的资源证据。
- **错误事件未做故障注入**：退出事件在洪泛下已实测保留；`TERMOLE_EVENT_ERROR` 需要让 `ReadFile` 以非 broken-pipe 错误失败，当前没有安全的注入点。保留策略由同一段 `push_event` 代码覆盖，但未单独实测。
- **句柄阈值与机器相关**：`+4`/`+8`/`+2` 和 64/32 MiB 阈值保留了本次实测的余量；不同 Windows 版本、杀毒软件或图形驱动可能改变首次初始化量，因此用例在预热后判断趋势。
- **Windows 内部资源**：锁定映像导致的重复增长已由预检消除；无效 PE 等仍可能在 `CreateProcessW` 内部留下 Windows 资源，但这类失败会触发加载器 UI，测试按 `docs/agents/testing.md` 不主动制造。
- **不是多小时 soak**：本次是分钟级高强度负载，不是数小时驻留测试；`t20_soak` 的 3×200,000 行和 20,000 次洪泛用于在 CI 时间内暴露趋势。
- **`125` 未故障注入**：与 T26 记录一致；GUI/基础设施失败的 `125` 分支仍无专门注入。
- **没有常驻服务**：本票没有新增 daemon、后台守护进程或轮询线程以外的常驻组件；View watcher 和 Session reader 都随对象销毁而结束。

## 复现

```bat
scripts\test-one.cmd termole_soak_test t20_soak
scripts\test-one.cmd termole_t03_t05_test t03_t05_session
scripts\test-one.cmd termole_launcher_test t26_launcher
build.cmd
```

`t20_soak` 的实测数值会随机器和运行时状态小幅变化；失败时输出会保留具体句柄数、working-set 数值和失败阶段。
