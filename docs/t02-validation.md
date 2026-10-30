# T02 任意目标程序启动验证

日期：2026-10-30。对应 [GitHub Issue #3](https://github.com/Coh1e/termole/issues/3)。本文是 T02 切片的实际执行证据；T02 完成不表示首版 MVP 完成。

## 结论

CLI 和公开宿主接口现在都能启动指定目标程序，并保留逻辑 argv、工作目录和每会话环境。目标程序由真实 ConPTY 承载；测试用受控控制台子程序把收到的 argv/cwd/env 与标准句柄类型写入报告，不依赖终端画面猜参数。

T02 同时修复了 T01 留下的自然退出问题：目标进程退出后由独立等待线程更新 `running()`，应用窗口能关闭；不再只依赖 ConPTY 输出管道 EOF。

## 实现要点

- C ABI 升级到 `TERMOLE_ABI_VERSION 2`；`termole_session_options` 接受 `arguments`/`argument_count` 与 `environment`/`environment_count`，不再把参数拼成一个字符串。
- 目标参数逐个使用 Windows/CRT 引号规则重建命令线；空参数为 `""`，保留空格、引号、反斜杠和 Unicode。
- 程序解析：带路径的输入转绝对路径并检查存在；裸名称通过 `SearchPathW` 查找；缺失返回 `NOT_FOUND`。
- 工作目录：验证存在且为目录，再转绝对路径；失败返回 `INVALID_ARGUMENT`。
- 环境：读取调用方环境，按大小写不敏感键叠加 `KEY=VALUE`，构造 Unicode 环境块并传 `CREATE_UNICODE_ENVIRONMENT`；调用方环境不变。
- CLI：`--cwd`、`--env`、`--help`、`--version`、`--`；首个非选项后不再解析 Termole 选项。
- 诊断：`AttachConsole(ATTACH_PARENT_PROCESS)` 或重定向句柄存在时写 stderr/stdout；无控制台时用 MessageBox。退出码：用法错误 2，启动失败 1，帮助/版本 0。
- 目标退出：新增 `exit_thread_` 等待进程句柄；进程退出后关闭伪控制台，让 reader 排空尾部输出，随后才置 `running=false`。T03 再处理非零退出码、最后画面保留与窗口策略。

## 体积与启动优化

本轮把产品入口与测试专用 smoke 完全分离：

- 产品 `termole.exe` 不编译 `TERMOLE_SMOKE_RESULT` 钩子，也不链接 `std::filesystem` 的 smoke 实现；测试使用独立的 `termole-smoke*.exe`。
- 全目标静态 CRT（`/MT`），Release 启用 LTO、`/OPT:REF`、`/OPT:ICF`、`/INCREMENTAL:NO`。
- 增加 `/GR-`、`/Gy`、`/Gw`、`/Zc:inline`、`/Oi`，减少 RTTI、未引用函数/数据和 COMDAT。
- 产品依赖仍只有系统 DLL；CLI 为 `CommandLineToArgvW` 引入 `SHELL32.dll`，其余依赖与 T01 相同。

Release 产物：

| 文件 | 字节 |
| --- | ---: |
| `termole.dll` | 268,800 |
| `termole.exe` | 152,576 |
| `termole-host.exe` | 123,392 |
| `termole-test-helper.exe` | 174,080 |

T01 提交时 `termole.exe`/`termole-host.exe` 约为 226 KB；把 smoke/`std::filesystem` 移出产品入口后分别降到约 152 KB/123 KB，再加入 CLI 解析后保持在该量级。

## 测试

`build.cmd` 实际执行结果：CTest 11/11 通过；连续运行 5 次，5/5 通过；PowerShell/并行 CLI 用例额外连续运行 10 次，10/10 通过。

| 测试 | 外部可观察证据 |
| --- | --- |
| `t02_host_args` | 宿主接口创建两个并发 Session，分别使用不同 argv、cwd、env；受控控制台子程序报告 argv 数量、每个参数的 UTF-8 字节长度与内容、cwd、环境值和 `stdout.console=1`。覆盖空格、引号、空参数、Unicode、反斜杠/内嵌引号、以 `-` 开头的参数、带空格的程序路径和工作目录。子程序退出前写 `T02-final-marker`；两个 Session 均先变为 `running=false`，且此时屏幕已包含该尾部标记。 |
| `t02_cli_args` | 通过 `execute_process` 以真实 argv 启动 `termole.exe`，经 `--cwd`/`--env`/`--` 传递同样复杂的参数；子程序报告逐项匹配。 |
| `t02_cli_help` | `termole.exe --help` 通过重定向 stdout 输出 Usage 并退出 0。 |
| `t02_cli_error` | 未知选项退出 2 并写 stderr；缺失程序退出 1 并写 stderr。 |
| `t02_cli_cmd` | 生成真实 `.cmd`，用 `start "" /wait` 启动目标；报告 cwd/env/`stdout.console=1`，验证 CMD 等待与标准句柄。 |
| `t02_cli_powershell` | 通过 Windows PowerShell `Start-Process -Wait` 启动目标；报告 cwd/env/`stdout.console=1`。 |
| `t02_cli_parallel` | 通过 PowerShell 同时启动两个独立 `termole.exe` 实例，分别使用不同 argv、cwd、env；两个报告互不覆盖，两个实例均退出 0。 |
| `t02_cli_no_console` | 测试用 `CreateProcessW(..., CREATE_NO_WINDOW)` + `SW_HIDE` 启动；目标仍收到真实 ConPTY（stdin/stdout/stderr 均为 console）、正确 argv/cwd/env，应用正常退出。 |

补充证据：

- 空参数在报告中表现为 `arg4len=0` 与空 `arg4=`。
- `中文-🐭` 的 UTF-8 字节长度与内容逐字节匹配。
- 调用方在创建 Session 前后 `TERMOLE_PROBE_VALUE` 与 sentinel 均未改变；子进程专属变量没有泄漏回调用方。
- 两个报告互不覆盖，且两个实例的 argv/cwd/env 各自正确，证明独立 `termole.exe` 进程互不影响；宿主侧另有同一进程内两个并发 Session 的用例。
- 子进程的 stdin/stdout/stderr 均通过 `GetConsoleMode` 确认为 ConPTY console；无控制台启动同样成立。
- `t02_cli_no_console` 的进程启动到目标完成/应用退出约 0.3 s（CTest 记录，含目标进程与 ConPTY 启动），只作为启动路径回归代理；正式启动延迟/空闲/吞吐基线仍属 T21。

## 真实窗口证据

T02 修改后的独立入口仍以默认 PowerShell 启动并正常绘制；窗口截图见 [t02-standalone.png](evidence/t02-standalone.png)。参数/环境/退出行为以受控子程序报告和 CTest 为准，截图只用于确认入口未被 CLI 改动破坏。

## 已知缺口

- 目标非零退出码、最后画面保留和窗口策略属于 T03，尚未实现；尾部输出排空已在 T02 验证（`T02-final-marker`）。
- 目标派生的后代进程若继续持有 ConPTY 输出句柄，关闭伪控制台可能等待；T02 未覆盖该有界收尾策略，留给 T03。
- T02 验证的是启动与参数保真；尚未在 Windows 10 22H2、干净无开发工具环境或 CI 干净机器上运行。
- `.bat`/`.cmd` 目标不自动包一层 `cmd /c`；需要用户显式指定 `cmd.exe /c script.cmd`，符合“不引入命令语言”的边界。
- 环境条目当前只支持覆盖/新增，不支持删除继承变量。
- `--help`/错误在无控制台且未重定向时使用 MessageBox，这是 GUI 应用的诊断呈现方式；不是任意脚本可解析的稳定 stderr 通道。
- 便携包、Windows Terminal 式异步/同步语义细化及 Shell 管道行为仍按后续任务推进。
