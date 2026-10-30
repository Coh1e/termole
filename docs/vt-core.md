# 最小 VT 核心范围

状态：T01 切片实现。代码为 Termole 原创，不复制、不链接 Contour、Microsoft Terminal 或 libghostty-vt 的源码。本文说明已实现子集、行为依据和明确缺口，避免把切片能力误称为完整终端。

## 已实现

入口是 `src/core/utf8.*` 与 `src/core/terminal.*`：

- 增量 UTF-8 解码：分块输入、跨读取边界的字符、非法序列替换为 U+FFFD，不抛异常。
- C0 控制：BEL（忽略）、BS、HT（8 列制表位）、LF/VT/FF、CR、SO/SI（忽略）、DEL（忽略）。
- ESC：`7`/`8` 保存恢复光标、`D` IND、`E` NEL、`M` RI、`c` RIS；`(`、`)`、`#` 等字符集设计序列忽略。
- CSI：`@ A B C D E F G H f J K L M P S T X a b c d e h l I m n s u Z` 与部分私有模式（`?7` 自动换行、`?25` 光标可见）。
- SGR：重置、粗体、暗色、斜体、下划线、反显、隐藏、16 色、256 色、24 位真彩色、默认前景/背景。
- OSC：`0`/`2` 窗口标题；BEL 或 ST 结束；OSC 52 剪贴板写入被忽略（终端输出不得直接写系统剪贴板）。
- DSR `5n`/`6n` 与主设备属性 `c` 的响应，写回 ConPTY 输入通道。
- 有界参数数组、有限长度 OSC/字符串状态；解析器不因异常序列无限增长。

屏幕模型包含固定字符网格、光标位置、基础颜色/属性、滚动、行内插入/删除、清屏/清行。`Session` 持有终端状态并消费 ConPTY；`View` 只读取快照并绘制，不在 UI 层实现终端协议。

为让真实 PowerShell/ConPTY 行编辑和提示符稳定工作，切片还支持少量超出“纯文字演示”的序列：DSR/DA 响应、IL/DL/ICH/DCH、256/truecolor SGR，以及忽略 OSC 52 剪贴板写入（安全约束）。它们仍是一个小的兼容包络，不代表完整 VT 实现；下面的缺口清单是明确边界。

## 行为依据与来源

实现按公开规范与成熟终端的行为对照编写：

- Paul Williams, *VT100 parser* / *VT500-series parser*（vt100.net）：状态机分层与 CSI 参数处理思路。
- Microsoft Learn, *Console Virtual Terminal Sequences*：ConPTY 输出、光标、擦除、SGR 和 DSR/DA 的 Windows 行为。
- xterm *Control Sequences* 文档：常用 CSI/OSC 语义与 256 色模型。
- ECMA-48：控制序列的通用语法。

上述资料用于行为依据；代码为本项目原创。Contour 与 Windows Terminal 仅作为技术验证和比较参考，不进入产品依赖；固定来源和许可边界见 [core-validation.md](core-validation.md) 与 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。

## 明确不包含（后续任务）

- alternate screen（`?47`/`?1047`/`?1049`）、滚屏区（DECSTBM）、回滚历史与 reflow。
- 双宽 CJK 占位、组合字符、Emoji/ZWJ 序列宽度与字体回退（T07/T08）。
- 鼠标报告、bracketed paste、焦点事件（T12）。
- IME 组合与候选窗口（T09）。
- 字符集切换、DCS/Sixel/Kitty graphics 等高级协议。
- 选择、滚动、剪贴板（T11）。

这些缺口在 T01 的验收中不承诺；当前默认 shell 的交互路径不依赖它们。
