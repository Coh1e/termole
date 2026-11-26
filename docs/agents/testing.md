# 验证指南

<a id="inner-loop"></a>

## 本地验证

在本机 Windows 11 x64 的项目根目录运行。先按[Windows 构建说明](../cross-platform-build.md#windows-构建)设置 NMake 和 MinGW-w64，测试还需要 Windows PowerShell。配置输出列出 fzf、WSL、Neovim、Helix 和 lazygit 的探测结果。窗口与截图测试需要交互桌面。

修改代码时，先构建受影响的目标，再按名称运行相关测试。下面以自动换行为例。

```sh
cmake --build --preset windows-mingw --target termole_wrap_test
ctest --preset windows-mingw -R t17_wrap --output-on-failure
```

用 `ctest --preset windows-mingw -N` 查看本机已注册的用例。提交前运行 `cmake --workflow --preset windows-mingw`，完成 Debug 配置、构建和测试。用 `git diff --check` 检查改动中的空白错误。可用 `git config core.hooksPath .githooks` 在提交时检查暂存内容。

修改 Markdown 后，从仓库根目录运行以下 PowerShell 命令。它检查 Git 已跟踪和新增未忽略文档中的相对链接与标题锚点。

```powershell
.\tests\check_package_markdown.ps1 -Root . -Files @(git ls-files --cached --others --exclude-standard -- '*.md')
```

## 按改动选择测试

| 改动 | 测试入口 | 核对结果 |
| --- | --- | --- |
| Windows 启动器与 CLI | `t26_launcher` | 通过公开入口 `termole`、`-e`、`--stdio` 核对等待、退出码、管道字节、慢读端、取消、启动失败和控制台行为。内部角色 `--gui` 只用于窗口策略或几何测试。 |
| 管道与并发调用 | `t26_cli_pipelines`、`t26_cli_parallel` | 前者在 CMD 和 PowerShell 中核对字节顺序、退出码、提前关闭、fzf 过滤和含 NUL 字节的文件重定向。后者核对并发 `-e` 的参数、工作目录与环境隔离。 |
| 备用屏 | `vt_adapter_contract`、`c_abi_characterization`、`visual_alt_screen` | 依次检查 VT 模式、Windows 会话 C 接口报告的状态，以及实际窗口显示的文字。 |
| 真实 fzf 画面与操作 | `visual_fzf`、`visual_fzf_interaction`、`visual_fzf_standalone` | 配置时找到 fzf 才注册。交互用例分别通过宿主视图和独立入口 `termole -e` 的 `WM_KEYDOWN`、`WM_CHAR` 驱动真实 fzf，逐步截图检查方向键、筛选、Enter 和 Esc。 |
| 鼠标、选择与粘贴 | `visual_input_interaction`、`visual_input_standalone` | 两个入口均向视图发送鼠标和键盘消息。受控子程序 `tests/visual/t12_input_child.cpp` 打开鼠标模式、括号粘贴和应用光标键，记录真实 ConPTY 输入字节。测试把这些字节与固定行带的像素一并核对。 |

fzf 从 stdin 读取候选，通过终端显示交互界面，把选中记录写入 stdout。普通启动成功后启动器即返回。`-e` 和 `--stdio` 等到子进程退出并返回其状态。`-e` 将子进程标准流连接到 ConPTY。`--stdio` 要求调用方 stdin 和 stdout 均为管道或文件，将结果字节直接交给调用方。[CLI 数据通道](../cli.md#数据通道语义) · [启动与等待](../adr/0007-console-launcher-wait.md)

要复现启动失败，独占打开真实可执行文件的副本，再让启动器运行它。把文本改名为 `.exe` 会弹出系统对话框，使测试等待人工操作。启动器的窗口内容冒烟检查失败时会保留 `window-capture.bmp`。

## 核对窗口像素

用 `ctest --preset windows-mingw-visual` 运行视觉用例。默认 Debug 测试也包含这些用例。调整 View、渲染、窗口位置或选择器时，用真实 HWND 的截图或像素验证可见行为。`screen_text()`、窗口句柄、重绘计数和窗口坐标可用来定位故障。切换默认视图前，在两种实现上对照输入法、选区、复制粘贴、焦点和窗口交互。物理输入和焦点切换需另在桌面操作。

手动取证时，把示例句柄换成目标 HWND。

```powershell
powershell -NoProfile -File scripts\capture-window.ps1 -Hwnd 0x1234 -OutputPath out\view.png
```

- **采集。** 脚本通过 `PrintWindow` 请求完整窗口内容，写出 PNG 和统计值，断言由测试执行。编译辅助代码的临时目录默认为 PNG 旁的 `capture-temp`，可用 `-TempPath` 指定。等待跨进程截图的测试需用 `MsgWaitForMultipleObjectsEx` 继续处理窗口消息。
- **判读。** `background` 是出现最多的颜色，`colors` 为颜色数。与背景任一颜色通道相差至少 24 的像素计入 `inkPixels`，其占比为 `inkRatio`。整窗内容要求至少 500 个此类像素，且占比至少 0.08%。`strongPixels` 和 `bandStrongPixels` 采用 96 的差值，帮助区分填充的选区和字形边缘。
- **定位。** fzf 用例分别检查候选行和指针行，防止只有页脚显示也通过。交互行带按实测的 `View::cell_height()` 定位，独立窗口用相同字体和 DPI 的探测视图测量。默认 Direct2D 目标固定为 96 DPI，单元格尺寸使用设备像素。若快照有字而窗口空白，先核对 DIP 与像素换算。
- **产物。** 每次运行把 PNG 和统计写入 `<build>/tests/visual/<场景>/`。fzf 的候选行和指针行另存为 `fzf-candidate.*` 和 `fzf-pointer.*`。失败时保留现场截图。[像素测试的验收依据](https://github.com/Coh1e/termole/issues/27)

## 验证 SDK 和便携包

`t22_sdk_consumer` 先把 SDK 安装到 `<build>/tests/sdk-consumer/package` 并检查已安装文档的相对链接和标题锚点。独立构建的 `tests/sdk-consumer` 和随包示例 `share/termole/examples/sdk-host` 通过 `find_package(Termole CONFIG REQUIRED)` 链接安装后的 SDK。测试宿主驱动真实 CMD 和 ConPTY，检查 ABI、资源所有权及错误处理，并将宿主和嵌入视图的 PNG、统计值写入 `<build>/tests/sdk-consumer/artifacts/`。

交付候选包时运行 `cmake --workflow --preset windows-mingw-release`。记录输出的 ZIP 摘要和解压目录，在该目录按便携包说明验收真实选择、预览、取消、输入法、焦点与首次可输入耗时。[Windows 便携包验收](../cross-platform-build.md#windows-便携包验收)
