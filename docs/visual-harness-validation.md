# 像素级 View/窗口回归护栏验证（#27）

- 任务：[#27](https://github.com/Coh1e/termole/issues/27)
- 关联 bug：[#26](https://github.com/Coh1e/termole/issues/26)
- 修复：#26 已修复；根因与修复后像素证据见 [fzf-blank-validation.md](fzf-blank-validation.md)
- 日期：2026-10-31
- 环境：Windows 11 x64 本机；MSVC 14.51（VS 18 BuildTools）、CMake 4.4.5、Ninja；Release；fzf 0.74.5；Windows PowerShell 5.1

## 交付

### `scripts/capture-window.ps1`

对指定 HWND 调用 `PrintWindow(..., PW_RENDERFULLCONTENT)`，写出 PNG，并报告宽容的像素统计：

| 字段 | 含义 |
| --- | --- |
| `captured` | `PrintWindow` 是否成功 |
| `width` / `height` | 截图尺寸（可用 `-Width`/`-Height` 显式传入，避免 DPI 虚拟化） |
| `colors` | 不同 RGB 值数量（仅报告，不参与断言；字体平滑关闭时可能只有 2 种） |
| `background` | 出现次数最多的颜色 |
| `inkPixels` | 任一通道与背景差 `>= 24` 的像素数 |
| `inkRatio` | `inkPixels / (width * height)` |
| `bandInkPixels` | 仅 `-BandTop..-BandBottom` 行内的 ink 像素数 |
| `strongPixels` | 任一通道与背景差 `>= 96` 的像素数（区分整块反显与字形抗锯齿） |
| `bandStrongPixels` | 仅行带内的 strong 像素数 |

`-StatsPath` 写 `key=value` 便于测试读取，`-Quiet` 抑制人工摘要输出。
脚本只负责截图和统计，阈值由调用方决定；不断言精确颜色、不做整图 hash。
背景取众数、ink 用通道差，是为了容忍字体抗锯齿、浅/深主题和 DPI 缩放。

### `tests/visual/`

- `alt_screen_child.exe`：向 ConPTY 写入 `ESC[?1049h ESC[2J ESC[H` 和固定文本
  （`ALT-SCREEN PIXEL PROBE` 加 12 行 ASCII），保持 60 s；测试结束由 Session 终止。
- `termole_visual_test.exe`：
  - `--mode alt-screen`：Session 运行上述受控子程序，创建 1000×640 的无边框顶层窗口和 View；
    等 `screen_text()` 含已知文本**且** `paint_count() > 0`（这两个只作为辅助信号），
    强制重绘后调用截图脚本；断言成功捕获、`inkPixels >= 500` 且 `inkRatio >= 0.0008`，
    并把首行已知文本映射到像素行带，断言行带内有非背景像素。
  - `--mode fzf`：`cmd.exe` 执行 `type candidates | fzf`，等 `screen_text()` 含 `banana`（辅助信号），
    先做同样的整窗 ink 断言，再把候选行和 `▌` 指针字形行映射到像素行带，分别断言行带内有非背景像素
    （候选 `>= 50`，指针 `>= 10`）。fzf 不存在时打印 SKIP 并返回 77。
  - `--mode fzf-interaction` / `--mode fzf-standalone`：T06 的真实 fzf 交互用例，
    按键走 View 的 `WM_KEYDOWN`/`WM_CHAR`，逐步截图并断言方向键移动、输入筛选、
    Enter、Escape、退出后恢复主屏与光标。证据见 [t06-validation.md](t06-validation.md)。
    行带用实测 `View::cell_height()`，不用 `height / rows` 的近似值。
- 截图跨进程完成，`PrintWindow` 会向目标窗口发送消息；测试等待截图进程时必须继续 pump 消息
  （`MsgWaitForMultipleObjectsEx`），否则捕获与 UI 线程互相等待。
- 每次运行都把 PNG 和统计写到 `${CMAKE_CURRENT_BINARY_DIR}/visual/<scenario>/`；
  fzf 的整窗、候选行带、指针行带分别保存为 `fzf.png`/`fzf-candidate.*`/`fzf-pointer.*`，
  alt-screen 的整窗与首行行带为 `alt-screen.png`/`alt-screen-line.*`。失败时这些文件保留供复核。

### CTest 表达

- `visual_alt_screen`：**默认 CTest 的一部分**，标签 `visual`。
- `visual_fzf`：仅在 `TERMOLE_FZF` 找到时注册（与 `t06_fzf` 相同的探测条件），
  标签 `visual`，`SKIP_RETURN_CODE 77`。fzf 缺失时与 `t06_fzf` 一样不注册；
  直接运行 `termole_visual_test --mode fzf` 会打印 SKIP 并返回 77。
- `visual_fzf_interaction` / `visual_fzf_standalone`：T06 的两个交互用例，注册条件与
  `visual_fzf` 相同，标签 `visual`；独立入口用例还依赖 `termole_app`。
- #26 修复后删除了 `known-red` 标签，视觉用例都进入默认套件，既有断言未改动；
  默认 preset `windows` 的排除规则保留（已没有测试带该标签）。

```bat
:: 默认套件（build.cmd 使用），包含全部视觉用例
ctest --preset windows

:: 只跑视觉标签
ctest --preset windows-visual
```

历史：#26 修复前 `visual_fzf` 带 `known-red` 标签并被默认套件排除；红色是真实断言失败
（退出码 1），不是 `WILL_FAIL`、不是 xfail。修复前的空白证据与根因分析见
[fzf-blank-validation.md](fzf-blank-validation.md) 与
[docs/evidence/visual-fzf-blank.png](evidence/visual-fzf-blank.png)。

## 证据

### 确定性 alt-screen（默认套件，绿）

```
window=1000x640 colors=28 background=#fafafa inkPixels=46324 inkRatio=0.072381
PASS: the capture script produced window statistics
PASS: the window contains non-background text pixels (>= 500 px and >= 0.000800 of the window)
band=0..28 bandInkPixels=1664
PASS: the known alt-screen line has non-background pixels
```

截图：`tests/visual/alt-screen/alt-screen.png`（构建目录），另存为
[docs/evidence/visual-alt-screen.png](evidence/visual-alt-screen.png)。画面可见
`ALT-SCREEN PIXEL PROBE`、字符表和 12 行文本；#26 修复后右侧不再被裁切
（旧截图是 DIP/像素重复缩放的状态）。

### 真实 fzf（#26 修复后，绿）

```
PASS: fzf painted its candidates into the session (supporting signal, not proof)
PASS: the capture script produced window statistics
window=1000x640 colors=45 background=#fafafa inkPixels=5653 inkRatio=0.008833
PASS: the window contains non-background text pixels (>= 500 px and >= 0.000800 of the window)
band=523..551 bandInkPixels=1469
PASS: the fzf candidate line has non-background pixels
band=494..522 bandInkPixels=707
PASS: the fzf pointer glyph has non-background pixels
```

截图：`tests/visual/fzf/fzf.png`（构建目录），另存为
[docs/evidence/visual-fzf-fixed.png](evidence/visual-fzf-fixed.png)；候选行带与指针行带
另存为 [visual-fzf-fixed-candidate.png](evidence/visual-fzf-fixed-candidate.png) 与
[visual-fzf-fixed-pointer.png](evidence/visual-fzf-fixed-pointer.png)。修复前的同一用例
只有背景色（`colors=1` / `inkPixels=0`）。

## 默认套件结果

- `build.cmd`（configure + 全量构建 + `ctest --preset windows`）：**33/33 通过**，本机最近一次 84.89 s；
  `visual_alt_screen` 与 `visual_fzf` 都在默认套件中运行并通过。
- `ctest --preset windows-visual`：**2/2 通过**（alt-screen 与 fzf）。
- `ctest --preset windows -R visual_fzf` 现在可以找到并运行该用例；`known-red` 标签
  删除后不再被 preset 的 `exclude` 命中。也可以继续用
  `ctest --test-dir .local/build/windows -L visual` 跑视觉标签。

## 未覆盖/限制

- 只支持当前桌面会话和单显示器；窗口必须在可捕获的桌面上。
- 阈值是“任一通道与背景差 `>= 24`”、`ink >= 500 px / 0.08%`、候选行带 `>= 50`、指针行带 `>= 10`；
  容忍抗锯齿与关闭字体平滑（只断言背景差，不依赖颜色数量），但并不做 OCR；
  极低对比度主题（文字与背景通道差全部 < 24）会漏计。
- fzf 视觉用例依赖 fzf 的 alt-screen 行为；升级 fzf 后需重跑 `windows-visual`。
- 截图依赖 Windows PowerShell 5.1；没有 `powershell.exe` 的环境不注册视觉测试。
- 像素存在只证明“有可见字形”，不证明文本内容正确；内容正确性由 Session 测试负责。
