# Windows 原生界面基础验证

2026-10-30，MSVC 19.51 / C++23 / CMake / Ninja，使用系统 DirectWrite、Direct2D、WIC 和 Win32；WIL 为无额外运行时依赖的头文件资源管理助手。

`spikes/native-view` 实际编译、链接、运行和 CTest 1/1 通过：

- 相同 ChildView 代码分别在独立窗口外壳和模拟宿主 HWND 内创建原生子窗口，检查父窗口及尺寸。
- 创建真实 Direct2D HWND render target，完成 DirectWrite 绘制及 EndDraw。
- 销毁第一子窗口后保留并更新外部文字快照，在另一宿主中重新创建并渲染；两个顶层宿主均仍存活。
- 使用同一绘制方法渲染到 WIC 位图，检查有足够非背景像素，并写出 PNG。已目视检查中文、组合重音、单色 Emoji 与多行文字。

证据：`.local/build/native-view/evidence/native-text.png`。窗口不显示，不抢用户焦点；PNG 是相同绘制方法的离屏结果，不是屏幕截图，也不能证明真实窗口的可见呈现效果。

## 复现

```powershell
.\spikes\native-view\build.cmd
```

这仅证明 Windows 绘制和子窗口基础可用。文字快照不是 Session，没有运行 Shell、终端网格、终端字体布局、真实会话挂载、滚屏、输入、IME、DPI 或颜色 Emoji 实现；不能将其称为已完成的 TerminalView/SDK。

设计参考：[微软 DirectWrite HelloWorld / 资源分离教程](https://learn.microsoft.com/en-us/windows/win32/directwrite/getting-started-with-directwrite)。使用系统 API，不引入额外 GUI 框架。
