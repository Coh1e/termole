# A5 · Windows 文件选择浮窗的取消和并发隔离

2026-11-22 在 Windows 11（build 26300）用 Windows PowerShell 5.1 启动随包的
`share/termole/examples/select-file.ps1 -Directory <folder>`。fzf 版本为
0.75.0 (c7bf2b8e)，最终 Windows ZIP 的 SHA-256 为
`8db4755adacb6a39c6d9323c23b4c0fce9317a156ae31e517024a2ec69418854`。

| 操作 | 发送消息前的时间 | 预览 PowerShell PID / 退出时间 | fzf PID / 退出时间 | 示例 PID / 退出时间 | 退出码 | stdout / stderr 字节 |
| --- | --- | --- | --- | --- | --- | --- |
| Esc | 02:07:39.1635909 | 18920 / 02:07:39.1663458 | 14984 / 02:07:39.1663433 | 8636 / 02:07:39.3095071 | 130 | 0 / 0 |
| 关闭窗口 | 02:07:46.1997513 | 18824 / 02:07:46.2016219 | 5864 / 02:07:46.2014757 | 15072 / 02:07:46.2431798 | 130 | 0 / 0 |
| 另一窗口按 Enter | — | — | — | 21888 | 0 | 108 / 0 |

表中时间均为 UTC。驱动在调用 PostMessage 前读取 Windows 高精度 FILETIME，检查消息发送成功，再从保留的进程句柄读取退出时间。两种取消操作均满足「发送前采样 ≤ 子进程退出 ≤ 示例退出」。驱动在第一、第三个窗口显示初始预览后截图，再移动到下一项触发新预览。它按父进程关系找到本次 fzf 启动的 PowerShell，确认该进程仍在运行，随即向 View 发送 Esc 或向顶层窗口发送关闭消息。

第一、第二个实例使用不同的目录和文件名。第一扇取消后，驱动向第二扇输入
`beta`，截图中只剩 `second-beta.txt`，右侧显示该文件的内容
`second beta preview`。按 Enter 后 stdout 逐字节匹配以下路径和 CRLF。

```text
D:\workspace\spec42-lifecycle\.local\验收 文件\4d6f2791c82a4a5fa75e44858803fdb5\second\second-beta.txt
```

[第一扇取消前](evidence/applet-a5-escape.png) ·
[另一扇继续筛选和预览](evidence/applet-a5-survivor.png) ·
[第三扇关窗前](evidence/applet-a5-close.png)

首次复核在第三个窗口被驱动识别前结束，原因未确定。上表和截图来自随后的完整复核。解包目录和候选目录均含中文和空格。将 fzf.exe 加入 PATH，在交互式 Windows 桌面中运行以下命令复核。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/visual/file-preview-lifecycle.ps1 -Package <解包目录> -Root .local/a5
```
