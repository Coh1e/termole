# ADR 0012：C 与 C++ 的职责边界

- 状态：accepted
- 日期：2026-11-20
- 相关：ADR 0008、ADR 0009、ADR 0010、ADR 0011

## 背景

目标曾写成“尽量 C 化”，并尝试把默认 GUI 整体切到 C View。实践表明：C 适合稳定 ABI、无 COM/STL 复杂度的核心与协议层；View 的输入语义、IME、选择/剪贴板、焦点与窗口编排在 C++ 中更易维护，强行 C 化会换来大量 parity 测试失败而收益有限。

用户明确：**不为 C 而 C，C 只做擅长的事**。

## 决策

| 层 | 语言 | 理由 |
| --- | --- | --- |
| VT 解析/终端状态 | C（libghostty-vt + `src/vt`） | 稳定 C ABI，外部可替换 |
| Snapshot/光标/模式/选择文本 ABI | C | 跨语言边界，无 C++ 类型 |
| 基础 cell 渲染器 | C（`src/gui/gdi_render.c`） | 直接 Win32/GDI，易测试 |
| QuickJS ABI | C（`include/termole/script.h`） | 稳定、可选、可替换 |
| server/client 协议编解码 | C（后续） | 二进制/管道层，无 UI 语义 |
| View/输入/IME/选择/剪贴板/焦点 | C++ | COM、状态机、与 Win32 消息模型复杂耦合 |
| CLI/config/launcher/app 编排 | C++ | 已有成熟实现与测试 |
| C GUI 角色 | 实验性 C，可选 | 保留为验证/嵌入路径，不强制成为默认 |

## 规则

1. 新代码优先放 C，当且仅当满足：
   - 接口可表达为 C ABI；
   - 不依赖 COM 生命周期、异常、`std::string`/容器等 C++ 语义；
   - 可以被 C 测试或外部宿主直接调用。
2. 以下留在 C++：输入语义（按键/IME/鼠标模式）、选择/复制粘贴、View 生命周期、CLI/config、异常边界。
3. 不再为“C 比例”重写 C++ View；C GUI 只有在**同场景视觉/交互测试全绿**后才可评估默认切换。
4. C++ 调用 C 时只经公共/内部 C ABI，不暴露 C++ 类型。

## 后果

- 默认 `--gui` 继续使用 C++ View；`--c-gui` 保留为可选实验角色。
- 删除 `TERMOLE_C_GUI` 环境路由，避免半成品默认切换。
- 后续优先产品能力（server/client、QuickJS、窗口定义）而非 C 比例。
- C 核心/渲染/ABI 的测试继续保留。

## 验证

- `cgui_` 6/6 通过（显式 `--c-gui`）。
- 默认路径的 characterization/visual/launcher 测试保持 C++ View。
