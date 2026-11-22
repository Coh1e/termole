# A5 · Windows 文件选择浮窗的取消和并发隔离

2026-11-22 在 Windows 11（build 26300）用 Windows PowerShell 5.1 启动随包的
`share/termole/examples/select-file.ps1 -Directory <folder>`。fzf 版本为
0.75.0 (c7bf2b8e)，Termole 可执行文件的 SHA-256 为
`97d1404d836c2e2e7c64b755acd0c8cad009941028c2c550ebe1670ac3067d2b`。

| 操作 | 预览 PowerShell PID / 退出时间 | fzf PID / 退出时间 | 示例 PID / 退出时间 | 退出码 | stdout / stderr 字节 |
| --- | --- | --- | --- | --- | --- |
| Esc | 22164 / 10:02:55.6437827 | 2664 / 10:02:55.6439706 | 23720 / 10:02:55.8167058 | 130 | 0 / 0 |
| 关闭窗口 | 10588 / 10:03:02.5442137 | 1816 / 10:03:02.5441721 | 19896 / 10:03:02.5948482 | 130 | 0 / 0 |
| 另一窗口按 Enter | — | — | 6292 | 0 | 97 / 0 |

时间来自 Windows 进程句柄，时区为 UTC+08。驱动在第一、第三个窗口显示初始预览后截图，再移动到下一项触发新预览。它按父进程关系找到本次 fzf 启动的 PowerShell，确认该进程仍在运行，随即向 View 发送 Esc 或向顶层窗口发送关闭消息。

第一、第二个实例使用不同的目录和文件名。第一扇取消后，驱动向第二扇输入
`beta`，截图中只剩 `second-beta.txt`，右侧显示该文件的内容
`second beta preview`。按 Enter 后 stdout 逐字节匹配以下路径和 CRLF。

```text
D:\workspace\spec42-lifecycle\.local\a5\4e4051da938644f8a8283e1f7e5beb3f\second\second-beta.txt
```

[第一扇取消前](evidence/applet-a5-escape.png) ·
[另一扇继续筛选和预览](evidence/applet-a5-survivor.png) ·
[第三扇关窗前](evidence/applet-a5-close.png)

将 fzf.exe 加入 PATH，在交互式 Windows 桌面中运行以下命令复核。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/visual/file-preview-lifecycle.ps1 -Package <解包目录> -Root .local/a5
```
