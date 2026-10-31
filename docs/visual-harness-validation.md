# 像素级 View/窗口回归护栏验证（#27）

- 任务：[#27](https://github.com/Coh1e/termole/issues/27)
- 关联 bug：[#26](https://github.com/Coh1e/termole/issues/26)
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
- 截图跨进程完成，`PrintWindow` 会向目标窗口发送消息；测试等待截图进程时必须继续 pump 消息
  （`MsgWaitForMultipleObjectsEx`），否则捕获与 UI 线程互相等待。
- 每次运行都把 PNG 和统计写到 `${CMAKE_CURRENT_BINARY_DIR}/visual/<scenario>/`；
  fzf 的整窗、候选行带、指针行带分别保存为 `fzf.png`/`fzf-candidate.*`/`fzf-pointer.*`，
  alt-screen 的整窗与首行行带为 `alt-screen.png`/`alt-screen-line.*`。失败时这些文件保留供复核。

### CTest 表达

- `visual_alt_screen`：**默认 CTest 的一部分**，标签 `visual`。
- `visual_fzf`：仅在 `TERMOLE_FZF` 找到时注册（与 `t06_fzf` 相同的探测条件），
  标签 `visual;known-red`，`SKIP_RETURN_CODE 77`。fzf 缺失时与 `t06_fzf`
  一样不注册；直接运行 `termole_visual_test --mode fzf` 会打印 SKIP 并返回 77。
- 默认 test preset `windows` 排除 `known-red` 标签；`windows-visual` preset 只跑 `visual` 标签：

```bat
:: 默认套件（build.cmd 使用），#26 修复前也保持绿色
ctest --preset windows

:: 复现 #26 的红色：确定性 alt-screen 绿、真实 fzf 视觉红
ctest --preset windows-visual
```

票面同时要求“#26 修复前 fzf 视觉用例为红”和“build.cmd 全量通过”。这里的表达是：

- 红色是**真实的测试失败**（退出码 1，整窗 ink 与候选/指针行带断言均失败），不是 `WILL_FAIL`、
  不是 xfail、也不是打印一行“已知失败”后返回 0；用 `ctest --preset windows-visual` 可稳定复现。
- 默认套件用标签排除已知红色用例，`build.cmd` 不被 #26 变红。
- #26 修复后，删除 `tests/CMakeLists.txt` 中 `visual_fzf` 的 `known-red` 标签，
  该用例自动进入默认套件；断言无需改动。`windows` preset 的排除规则可以保留（没有测试再带该标签）。

## 证据

### 确定性 alt-screen（默认套件，绿）

```
window=1000x640 colors=22 background=#fafafa inkPixels=90637 inkRatio=0.14162
PASS: the capture script produced window statistics
PASS: the window contains non-background text pixels (>= 500 px and >= 0.000800 of the window)
band=0..28 bandInkPixels=2778
PASS: the known alt-screen line has non-background pixels
```

截图：`tests/visual/alt-screen/alt-screen.png`（构建目录），另存为
[docs/evidence/visual-alt-screen.png](evidence/visual-alt-screen.png)。画面可见
`ALT-SCREEN PIXEL PROBE`、字符表和 12 行文本。

### 真实 fzf（known-red，红）

数据通道正常，窗口像素为空：

```
PASS: fzf painted its candidates into the session (supporting signal, not proof)
PASS: the capture script produced window statistics
window=1000x640 colors=1 background=#fafafa inkPixels=0 inkRatio=0
FAIL: the window contains non-background text pixels (>= 500 px and >= 0.000800 of the window)
band=523..551 bandInkPixels=0
FAIL: the fzf candidate line has non-background pixels
band=494..522 bandInkPixels=0
FAIL: the fzf pointer glyph has non-background pixels
```

`screen_text()` 摘录含 `▌ cherry`、`▌ banana`、`▌ apple`、`3/3`、`>`，但截图只有背景色。
这正是 #26 的空白窗口现象；截图：`tests/visual/fzf/fzf.png`（构建目录），另存为
[docs/evidence/visual-fzf-blank.png](evidence/visual-fzf-blank.png)。

## 默认套件结果

- `build.cmd`（configure + 全量构建 + `ctest --preset windows`）：**32/32 通过**，本机最近一次 81.29 s；
  `visual_alt_screen` 在默认套件中运行并通过，`visual_fzf` 因 `known-red` 标签被默认 preset 排除。
- `ctest --preset windows-visual`：`visual_alt_screen` 通过，
  `visual_fzf` 的整窗 ink 与候选/指针行带断言失败（退出码 8；单测退出码 1），复现 #26。
- `ctest --preset windows -R visual_fzf` 找不到测试：命令行的 `-R` 不会覆盖 preset 的
  `exclude` 标签，复现红色请用 `windows-visual` preset 或
  `ctest --test-dir .local/build/windows -L visual`。

## 未覆盖/限制

- 只支持当前桌面会话和单显示器；窗口必须在可捕获的桌面上。
- 阈值是“任一通道与背景差 `>= 24`”、`ink >= 500 px / 0.08%`、候选行带 `>= 50`、指针行带 `>= 10`；
  容忍抗锯齿与关闭字体平滑（只断言背景差，不依赖颜色数量），但并不做 OCR；
  极低对比度主题（文字与背景通道差全部 < 24）会漏计。
- fzf 视觉用例依赖 fzf 的 alt-screen 行为；升级 fzf 后需重跑 `windows-visual`。
- 截图依赖 Windows PowerShell 5.1；没有 `powershell.exe` 的环境不注册视觉测试。
- 像素存在只证明“有可见字形”，不证明文本内容正确；内容正确性由 Session 测试负责。
