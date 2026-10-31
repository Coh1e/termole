# Agent skills

## Issue tracker

规格和任务使用 Coh1e/termole 的 GitHub Issues。
操作前读取 docs/agents/issue-tracker.md。

## Triage labels

使用标准任务标签（含 `verify`），映射见 docs/agents/triage-labels.md。

## Domain docs

采用单一领域结构。探索代码或编写规格前，
按 docs/agents/domain.md 读取术语表和相关 ADR。

## Verification

改动 View、renderer、窗口或选择器行为前，读 docs/agents/testing.md。
界面结论必须用截图/像素证据，不能只用 `screen_text`、`viewport_text`、`MainWindowHandle` 或 `paint_count`。
