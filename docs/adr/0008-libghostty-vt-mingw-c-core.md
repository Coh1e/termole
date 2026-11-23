# 共享 VT 核心与视图边界

状态：已采纳（2026-11-19）。

Applet 和轻量终端共用 libghostty-vt 处理 VT 控制序列，包括光标移动、屏幕切换和字符显示状态。复用同一引擎，让各平台按同一套终端语义运行 TUI。

Termole 的 C 核心通过适配层读取引擎状态，向视图提供终端快照。视图读取快照并绘制窗口。对外提供最近最多 1000 行回滚历史。

核心接口见 [core.h](../../include/termole/core.h)，平台工具链和静态链接方式见[跨平台构建](../cross-platform-build.md)，固定依赖的来源、版本和校验值见[来源记录](https://github.com/Coh1e/termole/blob/main/third_party/libghostty-vt/SOURCE.txt)。

## Windows C View 原型

实验入口 `termole.exe --c-gui` 使用 C View，通过 Session C ABI 读取快照并交给 Win32 GDI 绘制。默认 `--gui` 使用 C++ View。[应用入口](https://github.com/Coh1e/termole/blob/main/apps/standalone/main.cpp) · [C View](https://github.com/Coh1e/termole/blob/main/src/gui/cview.c) · [GDI 渲染器](https://github.com/Coh1e/termole/blob/main/src/gui/gdi_render.c)

`gdi_render_contract` 和 `cview_contract` 检查渲染像素与 View 选区行为。`cgui_visual` 和 `cgui_config_visual` 检查实验入口的窗口像素、关闭退出和配置尺寸。切换默认 View 前，须与 C++ View 对照验证行为及像素，覆盖选区、剪贴板、IME、鼠标报告、回滚、调整尺寸及外观配置。[测试注册](https://github.com/Coh1e/termole/blob/main/tests/CMakeLists.txt) · [验证指南](../agents/testing.md)
