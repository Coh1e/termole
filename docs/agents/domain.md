# Domain docs

采用单一领域结构：根目录 GLOSSARY.md 和 docs/adr/。

- 探索代码、撰写规格或任务前，读取 GLOSSARY.md
  及涉及当前工作的 ADR。
- 使用术语表定义的名称。
- 与既有 ADR 冲突时，明确指出冲突及理由。
- 涉及外部程序、选择器、`-e`、`--stdio` 或 pipe 动作时，先读 ../adr/0006-terminal-selector-boundary.md。
- 涉及 launcher、GUI 角色、默认等待或退出状态时，读 ../adr/0007-console-launcher-wait.md。
- 已确认约束和后续修正见 ../design-decisions.md。
- 技术验证结果见 ../technical-validation.md；
  区分已验证能力、候选方案和未实现功能。
