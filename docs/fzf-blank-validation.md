# fzf 全屏 TUI 空白修复（#26）

- Bug：[#26](https://github.com/Coh1e/termole/issues/26)
- 像素护栏：[#27](https://github.com/Coh1e/termole/issues/27)，交付见 [visual-harness-validation.md](visual-harness-validation.md)
- 日期：2026-10-31
- 环境：Windows 11 x64 本机；显示缩放 150%（`GetDpiForWindow` = 144）；fzf 0.74.5；
  MSVC 14.51（VS 18 BuildTools）、CMake 4.4.5、Ninja；Release；窗口 1000×640

## 修复前现象

```bat
ctest --preset windows-visual
```

`visual_alt_screen` 绿，`visual_fzf` 红，且是真实断言失败：

```
PASS: fzf painted its candidates into the session (supporting signal, not proof)
window=1000x640 colors=1 background=#fafafa inkPixels=0 inkRatio=0
FAIL: the window contains non-background text pixels
band=523..551 bandInkPixels=0
FAIL: the fzf candidate line has non-background pixels
band=494..522 bandInkPixels=0
FAIL: the fzf pointer glyph has non-background pixels
```

`screen_text()` / `viewport_text()` 同时含 `▌ cherry`、`▌ banana`、`▌ apple`、`3/3`、`>`，
`paint_count` 也在增长，但窗口只有背景色。截图：
[docs/evidence/visual-fzf-blank.png](evidence/visual-fzf-blank.png)。

## 根因：DIP 被当成设备像素，坐标被缩放两次

- `Renderer::create_target()` 用默认 `D2D1::RenderTargetProperties()` 创建
  `ID2D1HwndRenderTarget`，它继承系统 DPI（本机 144，即 1 DIP = 1.5 px）。
- `Renderer::create_format()` 用 DirectWrite 测量单元格，得到的 `cell_width_` / `cell_height_`
  是 **DIP**；`View::on_size()`、行/列定位、鼠标和 IME 却把这些值当成**设备像素**。
- 150% 缩放下，22 行 × 28.102 DIP 在窗口里实际占 22 × 28.102 × 1.5 ≈ **927 px**，
  但客户端只有 640 px。fzf 的列表/页脚/输入行靠底部（0 基行 15..21），
  被画在 y ≈ 632 px 及其下方，整块落在窗口外；`Clear` 仍把窗口刷成 `#fafafa`，
  于是截图只有一种颜色、`inkPixels=0`，而终端文本完全正常。
- 同一根因也解释了为什么 alt-screen 用例一直是绿的：它的已知文本在顶部 0..14 行
  （y ≤ 590 px），仍在窗口内；但 75 列 × 13.195 DIP × 1.5 ≈ 1485 px > 1000 px，
  旧截图右侧被裁掉。[visual-harness-validation.md](visual-harness-validation.md) 的旧
  alt-screen 截图就是这种裁切状态。
- 定位时的临时插桩确认：渲染器确实收到了非空格、默认前景/背景色的单元格；
  `DrawTextW` 被调用、`EndDraw` 返回 `S_OK`；在 y=0 画红色矩形和硬编码文本都能被截到，
  只有底部真实文本不可见。因此不是数据通道、不是 alt-screen、不是字体或颜色问题，
  也没有使用 `--no-color`、`--filter` 或删除 alt-screen 绕过。

## 修复

`src/view/renderer.cpp::create_target()` 把渲染目标的 DPI 固定为 96：

```cpp
D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties();
properties.dpiX = 96.0f;
properties.dpiY = 96.0f;
```

于是 1 DIP = 1 设备像素，DirectWrite 的单元格度量、`View` 的网格、行/列坐标、
鼠标与 IME 定位都在同一坐标系里。字体仍按 `font_size_dip × dpi / 96` 计算
（`create_format()` 不变），所以 150% 下 16 DIP 仍是 24 px 高，物理字号不变；
`WM_DPICHANGED` 仍会重建文本格式和渲染目标。

## 修复后证据

```bat
ctest --preset windows-visual
:: 100% tests passed, 0 tests failed out of 2
```

| 场景 | colors | inkPixels | inkRatio | 行带 |
| --- | --- | --- | --- | --- |
| `visual_alt_screen`（整窗） | 28 | 46324 | 0.072381 | 首行 band 0..28 → 1664 |
| `visual_fzf`（整窗） | 45 | 5653 | 0.008833 | 候选 band 523..551 → 1469；`▌` 指针 band 494..522 → 707 |

截图（构建目录 `tests/visual/` 每次运行也会重新生成）：

- [docs/evidence/visual-fzf-fixed.png](evidence/visual-fzf-fixed.png)：fzf 候选、`▌` 指针、
  `3/3` 页脚和 `>` 输入行均可见。
- [docs/evidence/visual-fzf-fixed-candidate.png](evidence/visual-fzf-fixed-candidate.png)
  与 [docs/evidence/visual-fzf-fixed-pointer.png](evidence/visual-fzf-fixed-pointer.png)：
  候选行带与指针行带的截图。
- [docs/evidence/visual-alt-screen.png](evidence/visual-alt-screen.png)：修复后的 alt-screen
  整窗截图，右侧不再被裁切。

## 回归

- `tests/CMakeLists.txt` 删除了 `visual_fzf` 的 `known-red` 标签，现在标签只有 `visual`；
  断言未改动。找到 fzf 时它和 `visual_alt_screen` 一起进入默认 CTest。
- `windows` preset 的 `known-red` 排除规则保留，但已没有测试带该标签。
- 修复后 `build.cmd`（configure + 全量构建 + 默认 CTest）通过，`visual_fzf` 在默认套件内运行。
- 修复只改变渲染目标 DPI，不改变会话、数据通道、alt-screen 或 fzf 参数。

## 未覆盖

- #26 只关闭“可见性/空白”根因。方向键、输入筛选、Enter/Escape、退出后恢复主屏等交互
  已由 [T06 #7](https://github.com/Coh1e/termole/issues/7) 在
  [t06-validation.md](t06-validation.md) 以像素证据验收。
- 本机只验证了 150%（144 DPI）缩放和单显示器；100%/200% 缩放与多显示器未单独跑像素用例。
- fzf 0.74.5；升级 fzf 后需重跑 `windows-visual`。像素阈值与限制见
  [visual-harness-validation.md](visual-harness-validation.md)。
