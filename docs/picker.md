# Picker（历史；已被通用 execute 取代）

> **状态：superseded（2026-10-31）**。用户要求 foot 设计对齐：
> fzf 是外部选择器，Termole 只提供通用 execute、显式 `--stdio` 和终端/ConPTY。
> 本文只保留历史；当前决策见 [ADR 0006](adr/0006-terminal-selector-boundary.md)。

## 旧设计（不要实现）

旧设计把选择器做成 Termole 的 `--picker` 模式：Termole 读候选、启动 fzf、收集结果并转发 stdout。

## 取代方向

```text
candidates | termole --stdio fzf [fzf args...]
```

- 调用方 stdin/stdout 直接交给 fzf；
- fzf 的 UI 走 ConPTY；
- Termole 不解析候选/结果，不提供 `--picker`/`--picker-arg`；
- 选择器是外部 GUI 程序，不由 Termole 内嵌。

T26 已从 `termole.exe` 移除 `--picker`/`--picker-arg` 及其专用代码、错误码和测试；调用方数据通道改由 `--stdio` 提供。

具体契约以替代 spec / ADR 0006 为准。
