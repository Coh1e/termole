# A3 · Windows 文件选择浮窗的焦点和输入法

2026-11-22，使用 #38 的 Windows ZIP 中的 `termole.exe` 和原样随包的
`share/termole/examples/select-file.ps1`，由 Windows PowerShell
5.1.26100.10068 通过 `-Directory` 调用。fzf 版本为 0.75.0 (c7bf2b8e)，
Termole 可执行文件的 SHA-256 是
`38818c14fbdc6aa3f68f5a9861a3ee7dd85ef3c6afc17e9523f4c476e11867dd`。
测试目录只含 `alpha.txt` 和 `中文目标.txt`。

| 情形 | 实测结果 |
| --- | --- |
| 前台调用 | 启动脚本前，测试调用窗口持有前台。启动后 `GetForegroundWindow` 返回 Termole 顶层窗口，`GetGUIThreadInfo` 返回的键盘焦点属于其 `TermoleViewWindow` 子窗口。启动后未点击，也未请求激活 Termole。 |
| 后台激活受限 | 测试调用窗口持有前台，并成功调用 `LockSetForegroundWindow(LSFW_LOCK)`。脚本启动后前台仍属于调用窗口，Termole 的可见 View 已绘出两个文件名。 |
| View 输入与脚本返回 | 向上述后台 View 定向发送 `WM_CHAR` 输入 `alph` 后，fzf 显示 1/2 个匹配。向 View 发送 Enter，stdout 是一行 `D:\workspace\spec40-applet\.local\a3\files\alpha.txt`，stderr 为 0 字节，脚本退出码为 0。 |

前台与后台打开时，按实际 DPI 用 `PrintWindow` 捕获的 View 图像逐字节相同。
[打开后的两个候选](evidence/applet-a3-open.png) · [输入 alph 后的匹配](evidence/applet-a3-filtered.png)

## 待验收

系统列出了简体中文微软拼音，前台 View 线程的键盘布局为 `08040804`。
向已聚焦的 View 输入拼音时，`SendInput` 在第一个扫描码返回 0，
`GetLastError` 为 5（拒绝访问）。尚无拼音候选、选词上屏或由这次选择返回中文路径的证据。
后台测试也尚需一次实际点击和随后的键入，验证鼠标激活路径。

人工复核可沿用上述两个文件。从前台启动示例后直接输入 `zhongwen`，
保存输入法候选图，选「中文」并保存 fzf 筛选图，再按 Enter。核对脚本
stdout 的完整中文路径、空 stderr 和退出码 0。后台场景应在窗口仍未取得
前台时点击其 View，再执行同一组操作。
