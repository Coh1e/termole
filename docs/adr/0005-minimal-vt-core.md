# 首版切片使用项目内最小 VT 核心

T01 需要一个能真实运行 PowerShell、通过 ConPTY 接收输出、向宿主公开画面快照并驱动原生子窗口的最小终端核心。Contour 的 headless 核心虽已通过探针，但会带入 GSL、boxed-cpp、reflection-cpp、libunicode 四项依赖和一层上游构建适配；libghostty-vt 依赖 Zig 构建；Windows Terminal TerminalCore 仍牵连 XAML/MSBuild 与内部渲染依赖。按 [ADR 0004](0004-reuse-without-dependency-expansion.md) 的约束，这些都不应因首个小切片自动成为产品依赖。

因此 T01 采用项目内实现的最小 VT 子集，范围限定为真实 PowerShell 基础交互所需的文字、光标、擦除、SGR、滚动、标题和 DSR/DA。实现依据公开规范而非复制上游源码，来源和缺口记录在 [VT 核心范围](../vt-core.md)。`View` 只消费终端快照，终端协议不进入 UI 层。

后果是后续 CJK/组合字符、alternate screen、鼠标、IME 等任务需要继续扩展或替换该核心；若扩展成本超过依赖成本，可在获得真实行为证据后重新评估成熟实现。当前选择不改变“复用成熟方案、但不扩大依赖树”的原则。
