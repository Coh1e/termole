# 选择器结果使用独立数据通道

> 状态：**superseded by [ADR 0006](0006-terminal-selector-boundary.md)**（2026-10-31）。
> 用户明确要求 foot 设计对齐：终端不内置 picker，fzf/rofi 只是外部选择器。
> 本 ADR 仅保留历史；不要据本文实现 `--picker`。

真正的弹窗选择器是首版要求，仅显示 TUI 不足以满足调用脚本。显式 picker 模式从 stdin 读候选、向 stdout 返回原始选择结果、向 stderr 写诊断，交互显示走独立通道，不解析最终画面来推测结果。相比普通终端启动，这需要额外验证 Windows fzf 的控制台与标准句柄行为；首版保证目标是未经修改的 Windows 原生 fzf，具体机制尚待验证。
