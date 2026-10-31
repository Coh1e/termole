# T12 真实鼠标与 bracketed paste 端到端验收（#13）

- 任务：[#13](https://github.com/Coh1e/termole/issues/13)
- 前置：[#12](https://github.com/Coh1e/termole/issues/12)、[#7](https://github.com/Coh1e/termole/issues/7)
- 像素护栏：[#27](https://github.com/Coh1e/termole/issues/27)
- 日期：2026-10-31
- 环境：Windows 11 x64 本机；显示缩放 **150%**（`GetDpiForWindow` = 144，单显示器）；
  MSVC 19.51.36260.0；CMake 4.4.5；Release；受控程序经真实 ConPTY 运行

## 结论

真实 ConPTY 受控程序同时启用了鼠标报告与 bracketed paste。两个入口都完成了
滚轮、点击、拖动、本地选择/复制、Ctrl+V、Shift+Insert、关闭鼠标模式后的回退和
调整尺寸后的坐标核对。所有交互结论都有 `PrintWindow` 截图和固定行带像素断言；
`screen_text()` 只作为辅助信号，不单独作为通过条件。

| 交互 | 公开宿主入口（Session + View） | 独立入口（`termole -e`） |
| --- | --- | --- |
| 滚轮上报 | 在 `?1002` 下发送 `b=64/65` | 同左 |
| 点击上报 | SGR `ESC[<0;5;2M` + 释放 `m` | 同左 |
| 拖动上报 | 在 `?1002` 下发送 `b=32` | 同左 |
| 本地选择 + 复制 | Shift+拖动保留本地选择；Ctrl+C 写入系统剪贴板 | 同左 |
| 多行粘贴 | Ctrl+V 发送 `ESC[200~…ESC[201~` | 同左 |
| Shift+Insert | 走同一 bracketed paste 路径 | 同左 |
| 关闭 `?2004` | 粘贴为普通文本，无边界标记 | 同左 |
| 按键不误作粘贴 | 输入 `KEY-TYPED` 只产生普通文本 | 同左 |
| 应用光标键 `?1` | ↑ 发送 `ESC O A`，关闭后 ↓ 发送 `ESC [ B` | 同左 |
| 关闭鼠标模式 | 点击/拖动回到本地选择；滚轮滚动 View；程序不再收到鼠标事件 | 同左 |
| 调整尺寸 | 1000×640 75×22 → 900×560 68×19，点击仍映射到目标单元格 | 1055×674 79×23 → 900×560 68×19，同左 |

## 方法

- 受控程序：`tests/visual/t12_input_child.cpp`，经真实 ConPTY 启动。它启用
  `?1000/1002/1003/1006/2004` 中的指定组合和 `?1`，按原始 VT 字节解析 SGR/X10
  鼠标、bracketed paste、普通文本和 SS3 方向键，并把**精确输入字节**写入日志、
  把状态写到固定行。
- 鼠标输入：测试向 View HWND 投递 `WM_LBUTTONDOWN`、`WM_MOUSEMOVE`、
  `WM_LBUTTONUP`、`WM_MOUSEWHEEL`。没有调用 `View::report_mouse`、
  `Session::write` 或任何编码函数。View 的 `WM_MOUSEWHEEL`/`WM_LBUTTONUP`
  使用消息自带坐标，而不是全局光标位置，因此投递坐标就是被测坐标。
- 键盘输入：Ctrl+C/Ctrl+V 用 `WM_CHAR`；Shift+Insert 用
  `WM_KEYDOWN VK_SHIFT` → `WM_KEYDOWN VK_INSERT` → 抬起；方向键用
  `WM_KEYDOWN`/`WM_KEYUP`。View 在 `WM_KEYDOWN` 记录 Shift 状态并走
  `on_key_down(VK_INSERT) → paste()`，与硬件键路径一致。
- 剪贴板：使用真实系统剪贴板（`SetClipboardData`/`GetClipboardData`）。
- 像素：每一步用 `scripts/capture-window.ps1` 的 `PrintWindow(PW_RENDERFULLCONTENT)`
  截图，并按固定行（标题、模式、选择文本、鼠标、粘贴、文本）映射到像素行带；
  行带用实测 `View::cell_height()`。阈值沿用 #27 的宽容通道差思路：本用例取行带
  ink ≥ 40 px，选中块用 `bandStrongPixels` 前后差 ≥ 2000，空白顶行 ≤ 5 px。
- 截图是该步完成时的窗口；拖动截图在按住按钮期间采集，因此能直接看到 `b=32`。
  按下、拖动、释放和滚轮的精确字节仍以目标程序日志为准。
- 每次运行的 PNG 和 `key=value` 统计写到：
  - `<build>/tests/visual/input-interaction/`
  - `<build>/tests/visual/input-standalone/`

本机前台锁不允许后台测试窗口稳定取得前台，因此鼠标使用 View 窗口消息而不是
硬件 `SendInput`；消息仍完整经过 `View::handle_message`、ConPTY 和目标程序，
不是测试钩子。Shift+Insert 同样由 View 的按键消息处理，不使用内部调用。

## 已覆盖的鼠标模式序列

测试先发送 `?1000l ?1002l ?1003l ?1006l ?2004l`，再按阶段启用下表组合。
两个入口使用相同的序列和坐标。

| 阶段 | 模式序列 | 操作 | 目标程序记录 |
| --- | --- | --- | --- |
| SGR 点击/拖动/滚轮 | `?1002h ?1006h ?2004h` | 点击、拖动、滚轮上/下 | `EVENT\|sgr\|b=0…`、`b=32…`、`b=64…`、`b=65…` |
| 仅按下/释放 | `?1000h ?1006h` | 点击、拖动 | 有 `b=0` 按下/释放；拖动**没有** `b=32` |
| 任意移动 | `?1003h ?1006h` | 无按键移动 | `EVENT\|sgr\|b=35…` |
| X10 编码 | `?1000h`（`?1006l`） | 点击 | `ESC[M` + 3 字节，按下/释放 |
| 应用光标键 | `?1h` / `?1l` | ↑ / ↓ | `ESC O A` / `ESC [ B` |
| bracketed paste 开 | `?1002h ?1006h ?2004h` | Ctrl+V、Shift+Insert | `PASTE\|bracket=1`，线上字节含 `ESC[200~`/`ESC[201~` |
| bracketed paste 关 | `?1002h ?1006h` | Ctrl+V | `TEXT`，线上字节无边界标记 |
| 关闭鼠标 | 全部 `l` | 点击/拖动、滚轮 | 无 `EVENT`；View 本地选择和滚动 |

已验证的精确线上字节（宿主与独立入口相同）：

| 事件 | 字节 |
| --- | --- |
| SGR 点击按下/释放 | `1b 5b 3c 30 3b 35 3b 32 4d` / `…6d`（`ESC[<0;5;2M` / `m`） |
| SGR 拖动 | `ESC[<32;11;3M` |
| SGR 滚轮上/下 | `ESC[<64;5;2M` / `ESC[<65;5;2M` |
| `?1003` 任意移动 | `ESC[<35;7;4M` |
| X10 点击 | `ESC[M` + `20 25 22`（按钮 0，x=5，y=2）；释放按钮 `23` |
| 应用光标键 | `?1h` 后 ↑ = `ESC O A`；`?1l` 后 ↓ = `ESC [ B` |
| bracketed paste | `ESC[200~SELECT-ALPHA alpha ESC[201~` |
| Shift+Insert | `ESC[200~shift-insert-line-one CRLF shift-insert-line-two ESC[201~` |
| `?2004` 关闭 | `plain-paste-one CRLF plain-paste-two`，无 `ESC[200~` |

## 调整尺寸与缩放

| 入口 | 初始窗口 | 初始网格 | 调整后 | 调整后网格 | 调整后点击 |
| --- | --- | --- | --- | --- | --- |
| 宿主 | 1000×640 | 75×22 | 900×560 | 68×19 | 第 3 列第 1 行 → `x=4 y=2` |
| 独立 | 1055×674 | 79×23 | 900×560 | 68×19 | 第 3 列第 1 行 → `x=4 y=2` |

- 只覆盖 **150%（144 DPI）单显示器**。实测单元格 13.1953×28.1016 px。
- 100%、200%、其它缩放和多显示器组合未覆盖；改变系统缩放需要注销/重新登录，
  本轮没有做。行带按实测 `cell_height()` 映射，但这不是其它缩放已通过的证据。

## 像素证据

共 38 张截图（宿主 19、独立 19），全部由本轮测试产物复制到 `docs/evidence/`。

宿主入口（`input-interaction`）：

| 步骤 | 像素统计 | 截图 |
| --- | --- | --- |
| 模式启用 | 模式行带 ink 1799 / strong 1516 | [modes](evidence/t12-host-modes-on.png) |
| SGR 点击 | 鼠标行带 ink 2750 / strong 2312 | [click](evidence/t12-host-sgr-click.png) |
| SGR 拖动 | 行带 2817 / 2405（按住按钮时采集） | [drag](evidence/t12-host-sgr-drag.png) |
| SGR 滚轮 | 行带 2777 / 2346 | [wheel](evidence/t12-host-sgr-wheel.png) |
| `?1000` 点击 | 行带 2750 / 2312 | [1000](evidence/t12-host-1000-click.png) |
| `?1003` 任意移动 | 行带 2748 / 2321 | [1003](evidence/t12-host-1003-motion.png) |
| `?1` 应用光标键 | 状态行带 1802 / 1541；关闭后 2094 / 1791 | [app](evidence/t12-host-cursor-app.png)、[normal](evidence/t12-host-cursor-normal.png) |
| X10 点击 | 行带 2700 / 2279 | [X10](evidence/t12-host-x10-click.png) |
| 调整尺寸后 | 整窗 ink 17938；点击行带 2760 / 2316 | [resize](evidence/t12-host-after-resize.png) |
| 本地选择（Shift+拖动） | 行带 strong 2533 → 8053 | [selection](evidence/t12-host-selection.png) |
| 复制后 Ctrl+V | 粘贴行带 ink 3983 / strong 3363 | [bracketed paste](evidence/t12-host-bracketed-paste.png) |
| Shift+Insert | 粘贴行带 4746 / 3974 | [Shift+Insert](evidence/t12-host-shift-insert.png) |
| `?2004` 关闭 | 文本行带 2843 / 2440 | [plain paste](evidence/t12-host-plain-paste.png) |
| 按键 `KEY-TYPED` | 文本行带 1618 / 1457 | [typed](evidence/t12-host-typed-text.png) |
| 关闭鼠标后本地选择 | 行带 strong 2083 → 6731 | [mouse off](evidence/t12-host-local-selection-off.png) |
| 关闭鼠标后滚轮 | 顶行 ink 1288 → 0 → 1288 | [before](evidence/t12-host-scroll-before.png)、[after](evidence/t12-host-scroll-after.png)、[back](evidence/t12-host-scroll-back.png) |

独立入口（`input-standalone`）重复同一组交互，关键截图：

| 交互 | 截图 |
| --- | --- |
| 模式启用 | [modes](evidence/t12-standalone-modes-on.png) |
| SGR 点击 | [click](evidence/t12-standalone-sgr-click.png) |
| SGR 拖动 | [drag](evidence/t12-standalone-sgr-drag.png) |
| SGR 滚轮 | [wheel](evidence/t12-standalone-sgr-wheel.png) |
| `?1000` 点击 | [1000](evidence/t12-standalone-1000-click.png) |
| `?1003` 任意移动 | [1003](evidence/t12-standalone-1003-motion.png) |
| `?1` 应用光标键 | [app](evidence/t12-standalone-cursor-app.png)、[normal](evidence/t12-standalone-cursor-normal.png) |
| X10 点击 | [X10](evidence/t12-standalone-x10-click.png) |
| 调整尺寸后 | [resize](evidence/t12-standalone-after-resize.png) |
| 本地选择 | [selection](evidence/t12-standalone-selection.png) |
| bracketed paste | [paste](evidence/t12-standalone-bracketed-paste.png) |
| Shift+Insert | [Shift+Insert](evidence/t12-standalone-shift-insert.png) |
| `?2004` 关闭 | [plain paste](evidence/t12-standalone-plain-paste.png) |
| 按键 `KEY-TYPED` | [typed](evidence/t12-standalone-typed-text.png) |
| 关闭鼠标后本地选择 | [mouse off](evidence/t12-standalone-local-selection-off.png) |
| 关闭鼠标后滚轮 | [before](evidence/t12-standalone-scroll-before.png)、[after](evidence/t12-standalone-scroll-after.png)、[back](evidence/t12-standalone-scroll-back.png) |

## 复制证据与辅助信号

- **复制结论 = 选区像素 + 系统剪贴板文本。** 选区截图（`t12-host-selection.png` /
  `t12-standalone-selection.png`）证明选中块可见；Ctrl+C 后系统剪贴板精确等于
  `SELECT-ALPHA alpha`，关闭鼠标模式后精确等于 `SELECT-BETA one`。剪贴板内容不是
  可视对象，不能用截图代替，因此两者并列作为复制步骤的证据。
- 宿主 `View::selection_text()` 返回同样的两段文本；独立入口没有进程内查询 API，
  只用系统剪贴板和选区像素核对。
- 关闭鼠标模式后程序日志的 `EVENT|` 数量不变；这是“程序不再收到鼠标事件”的直接证据。
- `screen_text()`/`viewport_text()` 仍只用于定位行和等待状态，不作为可见性通过条件。

## 复现

```bat
cmake -S . -B out\build\windows -G "Visual Studio 18 2026" -A x64
cmake --build out\build\windows --config Release --target termole_visual_test termole_input_child termole_app
ctest --test-dir out\build\windows -C Release -R "visual_input_(interaction|standalone)" --output-on-failure
```

本轮实际构建目录是 `build/vs`（同一生成器），命令等价。默认 preset/Ninja 环境
也可以运行：

```bat
ctest --preset windows -R visual_input --output-on-failure
```

## 未覆盖/限制

- 使用受控 VT 程序，不是真实 fzf 的鼠标流程；真实 fzf 的键盘交互仍由
  [T06 验证](t06-validation.md) 覆盖。本票的验收标准允许“真实 fzf 或受控程序”。
- 只覆盖 150% 缩放、单显示器。100%/200% 和多显示器未执行。
- 只覆盖鼠标左键；View 目前不处理右键/中键，因此 `?1003` 的右/中键拖动未验证。
- 独立入口用实际 client 像素计算网格（79×23）。`--columns 80 --rows 24` 的
  borderless 窗口尺寸取整会少一格，本票没有修改这个窗口尺寸逻辑；坐标断言按实际
  网格核对，不能据此宣称 CLI 的 80×24 已精确达到。
- 鼠标通过 View 窗口消息注入；没有用硬件 `SendInput`。这覆盖了 View 的消息处理、
  ConPTY 编码、目标程序解码和像素结果，但不声称验证了物理鼠标驱动/前台激活路径。
- 不做 OCR；像素只证明“有可见字形/选择块”，精确文本由日志和剪贴板核对。
- 剪贴板测试需要可用的交互式桌面；无法打开剪贴板时用例会失败而不是静默跳过。
- 本机全量 CTest 仍受 nvim/WSL 等环境限制；本票运行的是两个新增视觉用例和相关
  既有用例，结果见下。

## 本轮测试结果

```text
ctest --test-dir build\vs -C Release -R "visual_input_(interaction|standalone)" --output-on-failure
2/2 passed: visual_input_interaction 23.1 s, visual_input_standalone 31.3 s

ctest --test-dir build\vs -C Release -L visual --output-on-failure
6/6 passed: visual_alt_screen, visual_input_interaction, visual_input_standalone,
            visual_fzf, visual_fzf_interaction, visual_fzf_standalone
```

相关既有用例 `t01_host_api`、`t03_t05_session`、`t01_app_window`：3/3 通过。
本机运行这些用例时需把 `TMP`/`TEMP` 指到可写目录；否则它们的临时报告文件写入会失败，
这是环境限制，不是 T12 行为。全量 `ctest` 仍受 nvim/WSL 等外部程序限制，本轮未把
它们计为 T12 结果。

## 验收标准映射

- 按目标程序启用的模式编码鼠标事件，并定义与本地选择的切换：覆盖 `?1000`、
  `?1002`、`?1003`、`?1006` 开/关和 X10/SGR；Shift+拖动在鼠标报告开启时仍为
  本地选择；模式关闭后点击/拖动回到本地选择。
- 键盘/应用光标模式：`?1h` 时 ↑ 精确发送 `ESC O A`，`?1l` 后 ↓ 精确发送 `ESC [ B`。
- bracketed paste 边界：开启时精确发送 `ESC[200~`/`ESC[201~`；关闭时发送原文；
  `KEY-TYPED` 只作为普通文本，不产生 `PASTE`。
- 复制：选中块像素 + 系统剪贴板精确文本；不是只断言内部选择字符串。
- 点击、滚轮、拖动、多行文本和调整尺寸后的坐标：两个入口都核对，调整尺寸后
  点击仍映射到 `x=4 y=2`。
- 独立与嵌入入口一致：同一受控程序、同一组模式序列、同一组坐标和字节断言在
  两个入口都通过；不是只测编码函数。
