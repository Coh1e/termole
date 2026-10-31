# Picker（历史；已被通用 execute 取代）

> **状态：superseded（2026-10-31）**。用户要求 `pass ls | foot -e fzf` 式语义：
> fzf/rofi 是外部选择器，Termole 只提供通用 execute 和终端/ConPTY。
> 本文只保留历史；替代 spec/ADR 待确认后落盘，勿据此新增实现。

## 旧设计（不要实现）

旧设计把选择器做成 Termole 的 `--picker` 模式：Termole 读候选、启动 fzf、收集结果并转发 stdout。

## 取代方向

```text
candidates | termole -e fzf [fzf args...]
```

- 调用方 stdin/stdout 直接交给 fzf；
- fzf 的 UI 走 ConPTY；
- Termole 不解析候选/结果，不提供 `--picker`/`--picker-arg`；
- rofi 同样是外部 GUI 选择器，不由 Termole 内嵌。

具体契约以替代 spec / ADR 0006 为准。
