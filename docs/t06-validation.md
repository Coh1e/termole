# T06 真实 fzf 交互验收（#7）

- 任务：[#7](https://github.com/Coh1e/termole/issues/7)
- 前置：#26（渲染目标 DPI 修复）、#27（像素护栏）
- 日期：2026-10-31
- 环境：Windows 11 x64 本机；显示缩放 **150%**（`GetDpiForWindow` = 144，单显示器）；
  fzf 0.74.5（`c0a193e0`，与 [picker-validation.md](picker-validation.md) 固定版本一致）；
  MSVC 19.51.36260.0；CMake 4.4.5；Release；窗口 1000×640（宿主）与 1068×702（独立入口）

## 结论

真实、未修改的 fzf 在两个入口都完成了完整交互：

| 交互 | 公开宿主入口（Session + View） | 独立入口（`termole -e`） |
| --- | --- | --- |
| 方向键移动 | ↑ 与 ↓ 都移动了可见选择块 | ↑ 移动了可见选择块 |
| 输入筛选 | 输入 `ban` 后 3 个候选变为 1 个 | 同左 |
| Enter 选择 | 选中 `banana`，退出码 0 | 同左 |
| Escape 取消 | 退出码 130，无选择输出 | 同左 |
| 退出后恢复主屏 | 主屏标记、光标、取消行均可见 | 同左 |

所有结论都有 `PrintWindow` 截图和行带像素统计；`screen_text()` 只用于定位行和作为辅助信号。
没有使用 `--filter`、`--no-color`，没有删除或绕过 alternate screen，也没有修改 fzf 或 Termole
的 alt-screen 实现。

## 方法

- fzf 以 `cmd.exe /D /Q /C "type candidates.txt | fzf"` 启动，候选为
  `apple` / `banana` / `cherry`。fzf 命令行没有任何额外参数；它自己进入/退出 alternate screen。
- 键盘从 **View 的输入路径**进入：宿主用例向 View HWND `PostMessage(WM_KEYDOWN/WM_CHAR)`，
  独立用例向 app 的 `TermoleViewWindow` 子窗口发送同样的消息。没有用
  `Session::send_key` 绕过 View 的按键编码。方向键因此走当前终端模式（DECCKM）的编码路径。
- 每一步用 [#27](https://github.com/Coh1e/termole/issues/27) 的
  `scripts/capture-window.ps1` 调 `PrintWindow(PW_RENDERFULLCONTENT)` 截图，并按行带断言：
  - `bandInkPixels`：与背景通道差 ≥ 24 的像素；
  - `bandStrongPixels`：与背景通道差 ≥ 96 的像素。fzf 的选中行是整块反显，强像素明显多于
    普通候选文字，因此用两行的强像素差值判断“选择块在哪一行”，不依赖精确颜色。
- 行带映射：宿主用例用 `View::cell_height()`（1000×640、22 行，单元格高 28.1016 px）；
  独立用例先用同进程、同默认字体/DPI 的探针 View 测出 `cell_height()`，再按 app 客户区高度
  得到 24 行。没有用 `height / rows` 的近似值，避免底部行带错位（#27 的旧映射在 22 行
  窗口上到第 17 行时已累计约 17 px 误差）。
- 两个用例都先截取 fzf 出现前的主屏（`MAIN-SCREEN-READY`），再在 Escape / Enter 之后
  截取同一主屏；主屏标记、取消行、光标和 fzf 页脚行带分别断言。

## 公开宿主入口证据

用例：`visual_fzf_interaction`（`tests/visual/visual_test.cpp` 的 `--mode fzf-interaction`），
1000×640，22 行。最近一次运行约 32 s。

| 步骤 | 像素证据 | 截图 |
| --- | --- | --- |
| fzf 初始 | 整窗 ink 5653；apple 行强像素 1651，banana 行 644 | [初始](evidence/t06-fzf-host-initial.png) |
| ↑ | apple 674，banana 1894；选择块移到 banana | [↑ 后](evidence/t06-fzf-host-after-up.png) |
| ↓ | apple 1651，banana 644；选择块回到 apple | [↓ 后](evidence/t06-fzf-host-after-down.png) |
| Escape | 主屏标记行 ink 1456；`CANCEL-EXIT-CODE=130` 行 1532；光标行 391；原 fzf 页脚行 0 | [Escape 后主屏](evidence/t06-fzf-host-after-escape.png) |
| 输入 `ban` | banana 行强像素 1933；原 cherry/banana 上方两行 ink 均为 0；页脚 `1/3` 行 2002 | [筛选后](evidence/t06-fzf-host-filtered.png) |
| Enter | 选中行 `banana` 529；`ENTER-EXIT-CODE=0` 行 1293；`MAIN-SCREEN-RESTORED` 行 1719 | [Enter 后主屏](evidence/t06-fzf-host-after-enter.png) |

辅助信号（不作为可见性证据）：Escape 后 `screen_text()` 含
`MAIN-SCREEN-READY` / `CANCEL-EXIT-CODE=130` 且不含 `banana`；Enter 后含
`banana` / `ENTER-EXIT-CODE=0` / `MAIN-SCREEN-RESTORED` 且不再含 fzf 页脚 `3/3`。

## 独立入口证据

用例：`visual_fzf_standalone`（`--mode fzf-standalone`），通过公开入口
`termole.exe --no-config --borderless --columns 81 --rows 25 --title <unique> -e cmd.exe ...`
启动真实 app，1068×702、24 行；按键发给 app 的 `TermoleViewWindow` 子窗口。
最近一次运行约 33 s。

| 步骤 | 像素证据 | 截图 |
| --- | --- | --- |
| fzf 初始 | 整窗 ink 5852；apple 行强像素 1644，banana 行 644 | [初始](evidence/t06-fzf-standalone-initial.png) |
| ↑ | apple 587，banana 1974；选择块移到 banana | [↑ 后](evidence/t06-fzf-standalone-after-up.png) |
| Escape | 主屏标记行 1456；取消行 1532；光标行 391；原 fzf 页脚行 0 | [Escape 后主屏](evidence/t06-fzf-standalone-after-escape.png) |
| 输入 `ban` | banana 行强像素 1894；上方两行 ink 均为 0；页脚行 2103 | [筛选后](evidence/t06-fzf-standalone-filtered.png) |
| Enter | 选中 `banana` 行 529；退出码行 1293；恢复行 1719 | [Enter 后主屏](evidence/t06-fzf-standalone-after-enter.png) |

独立入口没有 `screen_text()` 可用，文本内容只由截图人工核对；自动断言只检查像素行带。

## 复现

```bat
cmake -S . -B out\build\windows -G "Visual Studio 18 2026" -A x64
cmake --build out\build\windows --config Release
ctest --test-dir out\build\windows -C Release -L visual --output-on-failure
```

只跑 T06 两个用例：

```bat
ctest --test-dir out\build\windows -C Release -R "visual_fzf_(interaction|standalone)" --output-on-failure
```

每次运行把 PNG 和 `key=value` 统计写到构建目录的
`tests/visual/fzf-interaction/` 与 `tests/visual/fzf-standalone/`；失败时保留全部截图。

## 验收标准映射

- 真实 fzf 全屏列表、方向键、字符输入：两个入口都通过；按键经 View 的
  `WM_KEYDOWN/WM_CHAR`，方向键使用当前终端模式编码。
- VT/SGR、清屏、光标定位/形状、alternate screen 进入退出：fzf 实际进入/退出 alt-screen；
  退出后主屏标记与光标块可见；模糊的 SGR 通过选中块像素体现。
- 独立入口与公开宿主入口：两个固定版本交互用例都通过。
- 不宣称选择结果已返回调用脚本：Enter 的输出只落到 cmd 的主屏，没有验证 `--stdio`
  结果转发；该契约由 T16 负责。

## 缩放覆盖

- **只覆盖 150%（144 DPI）**。本机是单显示器 2560×1440、150% 缩放。
- **100%、200% 和其它缩放未覆盖**；多显示器与不同缩放组合也未覆盖。改变系统缩放在
  Windows 上需要注销/重新登录，本次没有做。像素行带依赖实测 `cell_height()`，不是固定
  像素常量，但这不等于已经在其它缩放上跑过。
- #26 的修复把 D2D 渲染目标固定为 96 DPI，因此 1 DIP = 1 设备像素；150% 下字体仍按
  `font_size_dip × 144 / 96` 生成，所以本轮的 24 px 字体/28.1016 px 单元格是 150% 的证据，
  不能外推到 100%/200%。

## 全量套件状态

`ctest --test-dir out\build\windows -C Release`：**27/35 通过**，4 个视觉用例全绿。
8 个失败与本改动无关，且都能在本机环境解释：

- `t01_host_api`：断言 `VX` 出现在 `screen_text()`，但当前 75 列窗口把 `V` / `X` 换行拆开；
  从更短的当前目录运行同一二进制时通过（退出码 0）。这是既有的换行敏感断言。
- `t18_nvim`、`t18_nvim_shell`、`t18_helix`、`t18_helix_shell`：本机沙箱拒绝编辑器
  启动/日志写入（`operation not permitted`、`os error 5`）。
- `t17_wsl`：沙箱拒绝 WSL 访问（`Wsl/E_ACCESSDENIED`）。
- `t02_cli_parallel`、`t03_cli_exit`：GUI 启动/关闭等待在本环境超时，与 fzf/像素用例无关。

本轮没有修改渲染器、View 输入编码或 alt-screen 代码；新增的是测试、截图统计字段和文档。

## 限制

- 不做 OCR；像素只证明“有可见字形/选择块”，文本内容由宿主用例的 `screen_text()` 或
  人工查看截图确认。
- 阈值是宽容的通道差（24 / 96），强像素比较依赖“选中行是整块反显”这一 fzf 默认外观；
  若升级 fzf 改变选中样式，需要重跑并调整行带比较。
- 固定 fzf 0.74.5；升级后重跑 `ctest -L visual`。
- 截图依赖 Windows PowerShell 5.1；没有 `powershell.exe` 时不注册视觉用例。
