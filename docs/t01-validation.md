# T01 双入口最小终端验证

日期：2026-10-30（Asia/Shanghai）。对应 [GitHub Issue #2](https://github.com/Coh1e/termole/issues/2)。本文记录实际执行证据，不表示首版 MVP 已完成。

## 结论

同一 `termole.dll` 中的 Session/View 已在两个入口运行真实 Windows PowerShell：

- 独立 `termole.exe` 窗口；
- `termole-host.exe` 最小原生宿主，在宿主自有布局中创建子 HWND。

两入口均具备基础 VT 文字/光标显示、可打印字符输入、Enter、退格、窗口尺寸同步和进程树收尾。测试通过公开 C ABI / C++23 包装或真实窗口入口观察行为，没有用 MockTerm 或静态快照代替终端。

## 环境

| 项 | 值 |
| --- | --- |
| OS | Windows NT 10.0.26300.0 x64 |
| 编译器 | MSVC 19.51.36260.0（Visual Studio 2026 BuildTools） |
| CMake / Ninja | 4.4.5 / 1.13.2 |
| 默认 Shell | Windows PowerShell 5.1.26100.9711 |
| PowerShell 7 | 未安装；回退路径已验证，并用临时 `PATH` 中的真实控制台程序命名为 `pwsh.exe` 验证优先选择路径 |
| 构建目录 | `.local/build/windows`（全新目录，无第三方联网依赖） |

PowerShell 7 优先探测在代码中实现（`ProgramFiles`/`ProgramW6432` 的 `PowerShell\7`、`LOCALAPPDATA\Microsoft\WindowsApps`、`PATH`）。本机没有真实 PowerShell 7，因此用复制为 `pwsh.exe` 的真实控制台 helper 验证“PATH 命中优先于 Windows PowerShell”，但没有验证真实 PS7 安装的交互行为。

## 构建与测试

```powershell
.\build.cmd
```

实际结果：配置与构建成功，CTest 3/3 通过；随后连续运行 5 次 CTest，5/5 通过。日志：`.local/product-build.log`、`.local/ctest-1.log` … `.local/ctest-5.log`。

| 测试 | 实际观察 |
| --- | --- |
| `t01_host_api` | 默认 PowerShell 会话创建；View 子 HWND 父窗口正确；同一 Session 的第二个 View 被 `INVALID_STATE` 拒绝；原始 Session 写入 + Enter 键执行 `termole-host-ok`；通过 View 的 `WM_CHAR` 输入、退格和 Enter 执行 `termole-VC`；窗口调整后 Session 网格和真实 `[Console]::WindowWidth/Height` 同步；直接 resize 到 42×13 后同样由 PowerShell 查询确认；受控控制台子程序验证清屏、光标定位、OSC 标题；临时 `PATH` 中的 `pwsh.exe` 优先于 Windows PowerShell；错误 ABI/参数返回明确状态；创建 `termole-test-helper.exe --sleep` 后代进程后只调用 `termole_session_destroy`，后代进程消失且无卡死。 |
| `t01_standalone_smoke` | 测试专用 `termole-smoke.exe` 真实启动 PowerShell，收到命令输出 `termole-smoke-ok` 后退出；屏幕快照写入 `.local/build/windows/tests/standalone-smoke.txt`。 |
| `t01_host_smoke` | 测试专用 `termole-smoke-host.exe` 同样运行真实 PowerShell 并收到 `termole-smoke-ok`。 |

命令标记在测试中由分离的字面量拼出（例如 `('termole-' + 'host-ok')`），所以 PowerShell 的输入回显不能单独满足断言；只有命令实际执行并输出后才会出现连续标记。

`t01_host_api` 的 VT 用例运行 `termole-test-helper.exe`（真实控制台进程），不是内存模拟：它输出清屏、`CUP` 定位、覆盖写和 OSC 标题，最终屏幕文本精确等于 `ROP\n  X`，标题为 `Helper Title`。

产品 `termole.exe` / `termole-host.exe` 不编译 `TERMOLE_SMOKE_RESULT` 钩子；smoke 入口由仅在 `BUILD_TESTING` 下构建的 `termole-smoke.exe` / `termole-smoke-host.exe` 驱动，不属于发布包。

## 真实窗口证据

在桌面会话中启动窗口、通过 View 的 `WM_CHAR` 投递命令，并用屏幕捕获记录（裁掉非客户区）：

- [独立窗口](evidence/t01-standalone.png)
- [最小宿主](evidence/t01-host.png)

两图均可见 Windows PowerShell 横幅、提示符、输入命令和实际输出。窗口截图只用于呈现证据；行为判定来自 CTest。

## 体积与依赖

Release 产物（`.local/build/windows/bin`）：

| 文件 | 字节 |
| --- | ---: |
| `termole.dll` | 252,416 |
| `termole.exe` | 225,792 |
| `termole-host.exe` | 226,816 |
| `termole-test-helper.exe` | 174,080 |

上表是 T01 完成时的数字；T02 加入 argv/env/CLI 与 smoke 分离后，当前体积见 [T02 验证报告](t02-validation.md)。

`dumpbin /dependents termole.dll` 只列出 `d2d1.dll`、`DWrite.dll`、`GDI32.dll`、`USER32.dll`、`KERNEL32.dll`；没有 `VCRUNTIME140.dll`、MSVCP 或第三方 DLL。`termole.exe` 只额外依赖 `termole.dll`。所有 Termole 目标使用静态 CRT（`/MT`），Release 启用 LTO、`/OPT:REF` 与 `/OPT:ICF`。

产品目标没有第三方库依赖，也不随包分发第三方源码；ConPTY 初始化沿用 MIT EchoCon 的公开模式并在 [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md) 署名。根构建入口加载 FetchContent 作为唯一依赖获取路径，但产品 configure 不调用任何 `FetchContent_MakeAvailable`，因此干净构建不访问网络；`TERMOLE_BUILD_SPIKES=ON` 时才获取 WIL/Contour 比较等探针源码。启动路径不初始化 QuickJS、WebView、Qt 或后台服务；Direct2D/DirectWrite 仅在创建 View 时惰性初始化。

独立入口 smoke 从进程启动到读到命令输出的实际 CTest 时间约 0.4–0.5 s，其中包含 PowerShell 启动与命令执行；这不是正式性能基准，T21 才建立可复现的启动/空闲/吞吐记录。

## 已知缺口

- 未用真实 PowerShell 7 安装验证交互；仅用 PATH 中伪装为 `pwsh.exe` 的真实控制台程序验证选择优先级。
- 未实现 alternate screen、滚屏/回滚、reflow、CJK 双宽、组合字符、Emoji 宽度、IME、鼠标、bracketed paste、选择/剪贴板和完整配置。
- 未在 Windows 10 22H2、干净无开发工具环境或 CI 干净机器上验证；未产出便携包。
- 进程树测试覆盖 `termole_session_destroy` 的正常释放路径；宿主进程崩溃或强杀时的孤儿回收留待 T03 的有界收尾策略。
- 桌面截图来自当前机器会话；不替代 Windows 10/11 全量交付验收。
