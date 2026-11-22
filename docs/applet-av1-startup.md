# AV1 · Windows 文件选择器冷启动

测量使用 Termole 0.1.0 Windows x64 ZIP，SHA-256 为
`8DB4755ADACB6A39C6D9323C23B4C0FCE9317A156AE31E517024A2EC69418854`。
Windows 11 x64 26H2 build 26300.10075，系统 Windows PowerShell
5.1.26100.10068，外部 fzf 0.75.0 (c7bf2b8e)。

| 候选包检查 | Windows 11 实测 |
| --- | --- |
| A1 选取 | [当前目录](evidence/applet-av1-a1-current.png)和[显式目录](evidence/applet-av1-a1-explicit.png)均直接运行解压包。stdout 分别为 103 和 105 字节，逐字节匹配完整路径加 CRLF。两次退出码均为 0，stderr 均为空。 |
| A2 前置条件 | 候选包的预检、预览及选择结果测试通过。 |
| A3 焦点及中文输入 | #40 的真实输入法组合输入仍待人工验证。 |
| A4 文件预览 | 驱动将解压包中的可执行文件与脚本复制到隔离目录。[文本](evidence/applet-av1-a4-alpha.png)、[二进制](evidence/applet-av1-a4-binary.png)、[锁定文件](evidence/applet-av1-a4-locked.png)和[特殊字符文件名](evidence/applet-av1-a4-target.png)均返回预期完整路径加 CRLF，退出码均为 0，stderr 均为空。 |
| A5 取消及并发 | 直接运行解压包。Esc、关闭窗口、另一窗口继续选择和进程回收见 [A5 实测记录](applet-a5-windows.md)。 |

Windows 10 22H2 x64 尚缺同一包的实测结果。#43 保持开放，性能门槛待跨系统比较后决定。

从仓库根目录运行，每次启动新的 PowerShell、Termole 和 fzf 进程。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/visual/fzf-startup.ps1 -Package "D:/workspace/spec31-applet/.local/便携 验收/Termole-0.1.0-win64" -Fzf "D:/playground/mingw64/bin/fzf.exe" -Root "D:/workspace/spec43-win11/.local/av1"
```

每次运行都创建独立的配置目录和候选目录。配置指定 80 列、24 行，用唯一窗口标题定位该次运行的 View。测试核对传给示例的目录枚举顺序为 `alpha.txt`、`beta.txt`。计时从已启动的 Windows PowerShell 5.1 调用包内原版 `select-file.ps1` 前开始，包括示例枚举文件、启动 Termole 和启动 fzf 的时间。文件系统缓存按机器当时状态保留。

驱动每 10 毫秒轮询一次。找到该窗口的 View 后只投递一次向上方向键，约 250 毫秒后投递 Enter。fzf 默认布局把第一个候选放在底部。只有示例返回第二个文件的完整路径、退出码为 0 且 stderr 为空，才确认此次方向键生效。

“投递前”和“投递后”读取同一台机器的单调时钟。投递后时间给出本次成功送入窗口输入队列的上界。投递前时间是 fzf 处理该按键的下界；示例交回选择结果时才确认 fzf 已处理，耗时包含等待 Enter、退出和 PowerShell 收尾。fzf 可能在投递前已经就绪，也可能在按键进入队列后才读取。该方法无法精确定位 fzf 内部的就绪时刻。

| 进程冷启动次数 | 投递前（ms） | 投递后（ms） | 确认返回（ms） |
| --- | ---: | ---: | ---: |
| 1 | 868.911 | 873.676 | 1247.894 |
| 2 | 616.534 | 621.501 | 983.774 |
| 3 | 610.283 | 615.021 | 966.547 |
| 4 | 607.319 | 612.322 | 978.168 |
| 5 | 588.810 | 593.702 | 948.457 |

五次均在空闲桌面执行，均只发送了一次方向键，返回各自目录的 `beta.txt`。成功按键入队时间的上界中位数为 615.021 ms，示例确认返回时间的中位数为 978.168 ms。
