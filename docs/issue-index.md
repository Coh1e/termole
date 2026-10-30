# Termole 首版任务索引

规格：[GitHub Issue #1](https://github.com/Coh1e/termole/issues/1)。24 项已发布；原生层级、46 条阻塞关系及 ready-for-agent 标签均已核对，父规格正文和状态保持原样。

任务详细内容和最新状态以 GitHub 为准。T 编号是已确认拆分中的编号；本地验收草稿见 [任务拆分](ticket-plan-v0.2.md)。

| 计划 | GitHub 任务 | 前置任务 |
| --- | --- | --- |
| T01 | [#2 在独立窗口和宿主内运行最小 PowerShell 终端](https://github.com/Coh1e/termole/issues/2) | 无，可开始 |
| T02 | [#3 通过 CLI 和宿主准确启动任意目标程序](https://github.com/Coh1e/termole/issues/3) | [#2](https://github.com/Coh1e/termole/issues/2) |
| T03 | [#4 正确处理失败退出、最后输出和进程树关闭](https://github.com/Coh1e/termole/issues/4) | [#3](https://github.com/Coh1e/termole/issues/3) |
| T04 | [#5 关闭视图后继续运行并恢复会话画面](https://github.com/Coh1e/termole/issues/5) | [#2](https://github.com/Coh1e/termole/issues/2) |
| T05 | [#6 宿主接收状态事件并安全处理销毁](https://github.com/Coh1e/termole/issues/6) | [#4](https://github.com/Coh1e/termole/issues/4) |
| T06 | [#7 在独立和嵌入终端中操作全屏 fzf](https://github.com/Coh1e/termole/issues/7) | [#3](https://github.com/Coh1e/termole/issues/3) |
| T07 | [#8 正确显示和操作中文双宽文字](https://github.com/Coh1e/termole/issues/8) | [#2](https://github.com/Coh1e/termole/issues/2) |
| T08 | [#9 正确显示组合字符和 Emoji](https://github.com/Coh1e/termole/issues/9) | [#8](https://github.com/Coh1e/termole/issues/8) |
| T09 | [#10 在独立窗口和宿主中使用中文输入法](https://github.com/Coh1e/termole/issues/10) | [#8](https://github.com/Coh1e/termole/issues/8) |
| T10 | [#11 调整字体主题并适应 DPI 变化](https://github.com/Coh1e/termole/issues/11) | [#8](https://github.com/Coh1e/termole/issues/8) |
| T11 | [#12 滚动选择并在终端和系统之间复制粘贴](https://github.com/Coh1e/termole/issues/12) | [#5](https://github.com/Coh1e/termole/issues/5)、[#9](https://github.com/Coh1e/termole/issues/9) |
| T12 | [#13 按终端模式处理鼠标和 bracketed paste](https://github.com/Coh1e/termole/issues/13) | [#7](https://github.com/Coh1e/termole/issues/7)、[#12](https://github.com/Coh1e/termole/issues/12) |
| T13 | [#14 用 CLI 将 TUI 呈现为独立 Applet 小窗口](https://github.com/Coh1e/termole/issues/14) | [#3](https://github.com/Coh1e/termole/issues/3) |
| T14 | [#15 用 TOML 和 Profile 复现启动配置](https://github.com/Coh1e/termole/issues/15) | [#4](https://github.com/Coh1e/termole/issues/4)、[#11](https://github.com/Coh1e/termole/issues/11)、[#14](https://github.com/Coh1e/termole/issues/14) |
| T15 | [#16 从调用脚本打开 fzf 弹窗并返回单选结果](https://github.com/Coh1e/termole/issues/16) | [#4](https://github.com/Coh1e/termole/issues/4)、[#7](https://github.com/Coh1e/termole/issues/7) |
| T16 | [#17 可靠返回多选、特殊字符和大规模选择结果](https://github.com/Coh1e/termole/issues/17) | [#9](https://github.com/Coh1e/termole/issues/9)、[#16](https://github.com/Coh1e/termole/issues/16) |
| T17 | [#18 在 CMD 和 WSL 中完成日常 Shell 交互](https://github.com/Coh1e/termole/issues/18) | [#4](https://github.com/Coh1e/termole/issues/4)、[#10](https://github.com/Coh1e/termole/issues/10)、[#13](https://github.com/Coh1e/termole/issues/13) |
| T18 | [#19 在 Neovim 和 Helix 中完成编辑任务](https://github.com/Coh1e/termole/issues/19) | [#9](https://github.com/Coh1e/termole/issues/9)、[#10](https://github.com/Coh1e/termole/issues/10)、[#13](https://github.com/Coh1e/termole/issues/13) |
| T19 | [#20 在 lazygit 中完成基本 Git 操作](https://github.com/Coh1e/termole/issues/20) | [#9](https://github.com/Coh1e/termole/issues/9)、[#13](https://github.com/Coh1e/termole/issues/13) |
| T20 | [#21 长时间输出与异常关闭时保持资源有界](https://github.com/Coh1e/termole/issues/21) | [#5](https://github.com/Coh1e/termole/issues/5)、[#6](https://github.com/Coh1e/termole/issues/6)、[#17](https://github.com/Coh1e/termole/issues/17) |
| T21 | [#22 按需刷新并建立可复现性能基线](https://github.com/Coh1e/termole/issues/22) | [#11](https://github.com/Coh1e/termole/issues/11)、[#21](https://github.com/Coh1e/termole/issues/21) |
| T22 | [#23 让外部应用仅凭开发包嵌入终端](https://github.com/Coh1e/termole/issues/23) | [#6](https://github.com/Coh1e/termole/issues/6)、[#10](https://github.com/Coh1e/termole/issues/10)、[#11](https://github.com/Coh1e/termole/issues/11)、[#13](https://github.com/Coh1e/termole/issues/13) |
| T23 | [#24 从干净构建生成便携包和开发包](https://github.com/Coh1e/termole/issues/24) | [#15](https://github.com/Coh1e/termole/issues/15)、[#17](https://github.com/Coh1e/termole/issues/17)、[#23](https://github.com/Coh1e/termole/issues/23) |
| T24 | [#25 在 Windows 10 和 Windows 11 完成首版交付验收](https://github.com/Coh1e/termole/issues/25) | [#18](https://github.com/Coh1e/termole/issues/18)、[#19](https://github.com/Coh1e/termole/issues/19)、[#20](https://github.com/Coh1e/termole/issues/20)、[#22](https://github.com/Coh1e/termole/issues/22)、[#24](https://github.com/Coh1e/termole/issues/24) |

执行时只选择此规格的子任务，所有前置任务关闭后才可开始。`ready-for-agent` 表示内容已可执行，不表示可以忽略阻塞关系。父规格本身不是待领取的实施任务。
