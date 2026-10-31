# Termole 设计讨论

## 已确认约束

- 项目使用 C++23，仅支持 Windows。
- 构建使用 CMake、FetchContent 和 Ninja。
- 优先借鉴成熟开源实现。若直接引入模块会牵连更多非必要依赖，则研究其成熟方案，只实现 Termole 所需部分，不为复用整套模块扩大依赖范围。这是用户对原先“直接集成优先”的纠正。
- 直接集成、抽取/移植及参考实现均应保留来源；复制或改编代码遵守相应许可证，不能以“参考重写”规避上游要求。
- 项目许可证为 MIT；依赖优先采用 MIT、BSD、Apache-2.0 等宽松许可证，其他许可证单独决定。
- 允许为减少自研调整原规格中的 VT 引擎和渲染技术；保留 Windows 原生、不使用完整跨平台 GUI 框架的约束。
- 首版目标为 Windows 10 22H2 / Windows 11、x64。
- 嵌入其他程序的能力是硬性要求，首版必须同时交付独立终端与最小 C++ 宿主嵌入示例。用户以 Windows Terminal 类似能力描述目标；具体接口与复用可行性待核实。
- 若引入 QuickJS，必须开放实际宿主能力，不能只作为一次性配置求值器。允许先设计好宿主能力的中间表示（IR）与接口，暂不接 QuickJS；具体含义及范围待确认。
- 技术选型优先以 C++ 实现整体方案；其他语言依赖不是默认选择，是否接受例外待后续明确。此前允许第三方非 C++ 的建议不作为已批准事项。
- 首版采用进程内原生子窗口嵌入；宿主管布局、焦点和生命周期，独立版复用同一控件。跨进程嵌入后续设计。
- 公共接口采用 C ABI 加 C++23 包装，首版验证 C++ 宿主。ABI 版本、数据所有权与线程契约仍待定义。
- 宿主能力 IR 指统一的结构化操作、查询与事件契约，供原生接口及未来 JavaScript 绑定复用；不自创脚本语言或虚拟机。
- 嵌入模式由宿主决定脚本可操作的会话及进程、文件、剪贴板权限，脚本不能自行扩大权限；独立版提供默认能力。
- 首版先完成宿主能力契约及原生实现，暂不接 QuickJS，后续再接 JavaScript。
- C++ 方案优先；缺少合适方案时可直接集成成熟 C 库，优先于用 C++ 重写。依赖 Zig 等额外工具链的方案另行决定。
- 首版一个 View 连接一个 Session；允许多个独立 Session/View，不提供同一 Session 的多 View 共享。
- 销毁 View 与终止 Session 是不同操作。宿主可销毁 View 并保留 Session；独立版关闭窗口默认终止会话。不提供宿主进程退出后的会话保活。
- 首批宿主操作：创建/终止会话、发送文本或按键、调整尺寸、应用外观、滚动和选择、复制粘贴。
- 首批宿主查询：会话状态、尺寸、标题、选中文本、退出码。事件：标题变化、响铃、子进程退出、错误。
- 创建进程可指定程序、参数、工作目录、环境变量。暂不提供通用文件与网络扩展 API；未来 QuickJS 复用宿主能力契约。
- View 在宿主 UI 线程创建、操作和销毁；内部处理终端 I/O，事件经宿主可调度队列交付，后台线程不直接调用宿主 UI。
- 参考 Ghostty 的设计与思路，以开箱即用优先；并不因此默认批准 Zig 工具链或选定 libghostty-vt。
- 首版保留 TOML、Profile，以及默认值 → 用户配置 → Profile → CLI 的覆盖顺序。嵌入宿主直接传设置，不强制读取用户配置；后续 JavaScript 复用设置模型。
- 默认管理会话启动的进程树，显式终止或宿主退出时清理；首版不支持子进程脱离会话后继续运行。正常退出先排空剩余输出，再报告完成。
- 没有 View 时继续消费输出，保留有界滚屏；重新附加 View 后恢复当前画面及保留历史，不因没有 View 阻塞子进程。
- 首版验收 PowerShell、CMD、WSL、fzf、Neovim、Helix、lazygit，以及中文输入、双宽字符、组合字符、Emoji 回退、复制粘贴、鼠标与调整尺寸；固定版本、记录可复现用例。高级协议逐项评估，不默认全部承诺。
- 操作返回结构化错误码及说明，异步失败经事件报告；C ABI 不传播 C++ 异常，库不自行弹错误对话框。独立版和嵌入宿主各自决定展示方式。
- 零配置启动优先使用已安装 PowerShell 7，否则 Windows PowerShell；默认 Windows 自带等宽字体，提供中文/Emoji 回退，主题跟随系统。不要求用户先安装 Shell、字体或编写配置。
- 独立版正常退出默认关窗，非零退出保留画面并显示退出码，可显式覆盖；嵌入模式由宿主决定。
- 无配置文件可正常启动；已有配置出错时显示文件及错误位置，提供使用默认配置启动的选择。CLI 参数错误直接报错退出。
- 首版交付解压可运行的便携包，包含必要运行库和许可声明；另提供嵌入开发包、最小宿主示例及接口文档。开发工具不要求最终用户安装。
- 终端不内置选择器：fzf 等程序拥有自己的候选、UI 和结果；Termole 不解析选择结果。选择器结果返回不属于终端 API。
- 嵌入开发包首版提供 DLL、C ABI 和 C++23 包装；库释放自身分配资源，校验接口版本；静态库后续按需增加。
- 独立版首版单窗口单会话，不增加标签页、分屏、全局快捷键呼出或后台常驻；宿主可自行组合多个控件。
- 默认允许用户主动复制粘贴，禁止终端输出通过控制序列直接写入系统剪贴板；用户或宿主可显式开启。本地与远端输出规则一致。
- `-e <program>` 对齐 foot execute：子进程 fd 0/1/2 接 ConPTY，不转发调用方 stdin/stdout；现有 `--` 目标分隔符保留。
- `--stdio <program>` 是 Termole 显式扩展：调用方 stdin/stdout 接程序，stderr/UI 走 ConPTY；对任意程序成立，不要求 fzf，不解析结果。
- `pipe-visible` / `pipe-selected` / `pipe-scrollback` 对齐 foot 单向 pipe：终端文本送到外部命令 stdin，命令 stdout/stderr 丢弃；命令内部可运行 fzf 等选择器并执行动作。
- `pipe-command-output` 依赖 shell OSC 133，单独立项。fzf 仅作为外部程序示例。
- 正常转交 `-e`/`--stdio` 子进程退出码；不额外定义 picker 专用取消码。Termole 启动失败提供独立错误说明。
- 首版切片采用项目内最小 VT 子集，不引入 Contour/libghostty-vt；见 [ADR 0005](adr/0005-minimal-vt-core.md) 与 [VT 核心范围](vt-core.md)。这是对 T01 的实现决定，不改变后续按证据评估成熟实现的约束。

## 输入资料

- [原始产品与技术规格 v0.1](product-spec-v0.1.md)：保留原文。与后续用户明确决定冲突时，以明确决定为准。
- T01 最小切片已实现；完整首版仍未完成，证据见 [T01 验证](t01-validation.md)。

## 进入技术验证前

产品和架构边界已逐轮确认；用户已确认共同理解并同意进入技术验证。技术验证计划及阶段结果见 [technical-validation.md](technical-validation.md)。

终端核心与渲染实现尚未选定；`-e`、`--stdio` 和 pipe-* 的边界已按 foot 设计核查确认，见 [ADR 0006](adr/0006-terminal-selector-boundary.md)。验证若发现必须引入额外工具链、修改 fzf 或削减已确认能力，应回到设计讨论，不静默改变约束。

## 初步事实核查（2026-10-30）

- [libghostty-vt](https://github.com/ghostty-org/ghostty#cross-platform-libghostty-for-embeddable-terminals) 官方说明支持 Windows/C，API 尚未稳定；它不是现成 Windows 终端控件。[官方 CMake 接入](https://github.com/ghostty-org/ghostty/blob/main/dist/cmake/README.md) 仍依赖 Zig 构建。
- Windows Terminal 为 MIT，但 [TermControl](https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalControl/TermControl.xaml) 依赖 XAML，[构建](https://github.com/microsoft/terminal/blob/main/doc/building.md) 使用 VS/MSBuild 等工具；不能直接当作已验证的纯 Win32/CMake 可嵌入库。
- [QuickJS-NG](https://quickjs-ng.github.io/quickjs/building/) 以 CMake 为主并支持 Windows，是后续接入候选。
- 以上为上游源码及文档核查，尚未在 Termole 中构建验证。

以上未确认项不得视为实现授权或既定架构。
