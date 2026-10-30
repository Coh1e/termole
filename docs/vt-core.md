# 最小 VT 核心范围

状态：T01–T12 切片实现（T09 IME 除外）。代码为 Termole 原创，不复制、不链接 Contour、Microsoft Terminal 或 libghostty-vt 的源码。本文说明已实现子集、行为依据和明确缺口，避免把切片能力误称为完整终端。

## 已实现

入口是 `src/core/utf8.*` 与 `src/core/terminal.*`：

- 增量 UTF-8 解码：分块输入、跨读取边界的字符、非法序列替换为 U+FFFD，不抛异常。
- C0 控制：BEL（转成宿主事件）、BS、HT（8 列制表位）、LF/VT/FF、CR、SO/SI（忽略）、DEL（忽略）。
- ESC：`7`/`8` 保存恢复光标、`D` IND、`E` NEL、`M` RI、`c` RIS；`(`、`)`、`#` 等字符集设计序列忽略。
- CSI：`@ A B C D E F G H f J K L M P S T X a b c d e h l I m n r s u Z` 与私有模式（`?1`、`?6`、`?7`、`?25`、`?47`/`?1047`/`?1049`、`?1000`/`?1002`/`?1003`、`?1006`、`?2004`）。
- SGR：重置、粗体、暗色、斜体、下划线、反显、隐藏、16 色、256 色、24 位真彩色、默认前景/背景。
- OSC：`0`/`2` 窗口标题；BEL 或 ST 结束；OSC 52 剪贴板写入被忽略（终端输出不得直接写系统剪贴板）。
- DSR `5n`/`6n` 与主设备属性 `c` 的响应，写回 ConPTY 输入通道。
- 主/备用屏幕、DECSTBM 滚屏区、1000 行有界回滚、光标原点模式与应用光标模式。
- 双宽 CJK、常见组合字符、变体选择符、Emoji 与 ZWJ 序列按单字素单元存放；快照、选择文本和绘制共用同一单元模型。
- 鼠标按下/拖动/移动模式与 SGR/X10 编码，bracketed paste 边界，终端本地选择与宿主剪贴板操作。
- 有界参数数组、有限长度 OSC/字符串状态、有界历史与事件队列；解析器不因异常序列无限增长。

`Session` 持有终端状态并消费 ConPTY；`View` 只读取快照并绘制，不在 UI 层实现终端协议。滚动、选择、剪贴板和鼠标模式由 View 编码为 Session 的输入或查询。

## 行为依据与来源

实现按公开规范与成熟终端的行为对照编写：

- Paul Williams, *VT100 parser* / *VT500-series parser*（vt100.net）：状态机分层与 CSI 参数处理思路。
- Microsoft Learn, *Console Virtual Terminal Sequences*：ConPTY 输出、光标、擦除、SGR 和 DSR/DA 的 Windows 行为。
- xterm *Control Sequences* 文档：常用 CSI/OSC 语义、备用屏幕、滚屏区、鼠标与 bracketed paste。
- Unicode Standard Annex #11/#29：东亚宽度与字素簇边界；实现采用面向终端的保守子集，不替代完整 UAX 库。
- ECMA-48：控制序列的通用语法。

上述资料用于行为依据；代码为本项目原创。Contour 与 Windows Terminal 仅作为技术验证和比较参考，不进入产品依赖；固定来源和许可边界见 [core-validation.md](core-validation.md) 与 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。

## 明确不包含（后续任务）

- 完整 UAX #29 字素切分、全部区域指示符/旗帜序列、彩色 Emoji 字形保证；当前只保证终端网格的单元格与常用组合/ZWJ 序列。
- 自动 reflow（窗口宽度变化后的历史重排）。
- IME 组合与候选窗口（T09）。
- 字符集切换、DCS/Sixel/Kitty graphics 等高级协议。
- 终端输出触发的系统剪贴板写入（安全约束，OSC 52 被忽略）。
- 焦点事件、完整 modifyOtherKeys/kitty keyboard 协议。

这些缺口在对应验收任务完成前不承诺；当前默认 shell、fzf 和基础选择/粘贴路径不依赖它们。
