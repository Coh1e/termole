# 领域模型：Termole

状态：待评审（Phase 0）
用途：在 TDD 迁移前对齐术语、边界、不变量与验收示例。
本文档只描述设计与需求，不包含实现改动。

来源：

- [GLOSSARY.md](../GLOSSARY.md)
- [ADR 0001](adr/0001-native-embedding.md)、[0002](adr/0002-host-capability-contract.md)、
  [0006](adr/0006-terminal-selector-boundary.md)、[0007](adr/0007-console-launcher-wait.md)
- [design-decisions.md](design-decisions.md)
- [host-api.md](host-api.md)、[cli.md](cli.md)、[config.md](config.md)
- [implementation-spec-v0.3.md](implementation-spec-v0.3.md)

原设计冻结：术语、公共 ABI、生命周期、配置/launcher 行为、安全规则不改。
实现栈（libghostty-vt、MinGW、C23、C GUI、静态链接、MSYS Makefiles）另行执行。

---

## 1. 统一语言

### 1.1 既有术语（来自 GLOSSARY，不重命名）

| 术语 | 定义 | 上下文 | 计划归属 |
| --- | --- | --- | --- |
| 会话（Session） | 一次终端程序运行及其终端状态，可无视图存在 | Session/Process | `src/session` |
| 视图（View） | 呈现一个会话并接收交互的界面；销毁不终止会话 | View/Window | `src/ui`、`src/render` |
| 嵌入宿主（Embedding Host） | 用 SDK 嵌入终端的应用，管布局、寿命、能力 | Embedding SDK | `src/api` |
| 交互宿主（Interaction Host） | 独立入口为外部程序提供窗口与交互 | Launcher/View | `apps` |
| Launcher | `termole.exe` 控制台角色，解析参数并启动 GUI 角色 | Launcher | `apps` |
| GUI 角色 | 同一 exe 的 `--gui` 内部角色，建窗口/Session/View | Launcher/View | `apps` |
| Profile | 一组可复用的终端启动设置 | Configuration | `apps` |
| 选择器（Selector） | 外部程序自管候选与结果；Termole 不实现结果协议 | Data Channel | `src/session` |
| 执行模式（Execute，`-e`） | 程序在 ConPTY 中运行，不转发调用方 stdio | Launcher | `apps` |
| 标准流模式（Stdio，`--stdio`） | 调用方 stdin/stdout 接程序，UI/stderr 走 ConPTY | Data Channel | `src/session` |
| 数据通道（Data Channel） | 与显示分离的字节路径 | Data Channel | `src/session` |
| 管道动作（Pipe action） | 视图/选区/回滚文本送到外部命令 stdin | View/CLI | `apps` |
| Pip | 品牌角色，不是模块名 | - | - |

### 1.2 新增候选术语（评审后决定是否进入 GLOSSARY）

标记为“候选”的术语不改变既有术语含义，只用于本次实现讨论。

| 候选术语 | 定义 | 状态 |
| --- | --- | --- |
| 终端状态（Terminal State） | Session 内由 VT 引擎维护的屏幕、光标、模式、回滚、选择 | 候选（VT 领域） |
| 帧（Frame） | 某一时刻终端状态的只读投影，供渲染消费；有明确有效期 | 候选（VT/View 边界） |
| 窗口定义（Window Definition） | 声明窗口尺寸、位置、装饰、DPI、标题栏等属性的值对象 | 候选（配置扩展） |
| 服务端（Server） | 可选的常驻进程角色，托管多个窗口并共享字体/配置/脚本 | 候选（扩展） |
| 客户端（Client） | 向 Server 请求开窗并等待退出码的角色 | 候选（扩展） |
| 脚本运行时（Script Runtime） | 执行本地 `init.js` 的 QuickJS 实例，受能力约束 | 候选（扩展） |
| 宿主能力（Host Capability） | 宿主授予脚本/绑定的操作与资源集合 | 候选（已有 IR 草案） |
| 事件队列（Event Queue） | 有界的会话事件缓冲，由宿主调度消费 | 既有实现，补术语 |

---

## 2. 上下文地图

| 上下文 | 职责 | 不负责 | 与其他上下文关系 |
| --- | --- | --- | --- |
| Terminal Emulation | VT 解析、屏幕/回滚、选择、模式、输入编码 | 窗口、进程、配置 | 被 Session 拥有；向 View 提供 Frame |
| Session / Process | ConPTY、进程树、I/O 线程、退出与排空、事件队列 | 窗口布局、渲染 | 拥有 Terminal State；0..1 View |
| View / Window | HWND、消息循环、渲染、IME、剪贴板、滚动交互 | VT 语义、进程寿命 | 依附 Session；消费 Frame |
| Configuration | 默认值、TOML、Profile、窗口定义、优先级、诊断 | 执行脚本（扩展前） | 输入 Launcher/GUI；可热重载 |
| Launcher / CLI | 参数、控制台角色、启动 GUI、等待与退出码 | 渲染、VT | 启动 GUI 角色；可转发到 Server |
| Embedding SDK | C ABI v3、C++ 包装、所有权/线程契约 | 应用业务 | 暴露 Session/View 能力 |
| Server / IPC（扩展） | 常驻进程、多窗口、命名管道、共享资源 | 改变单进程语义 | 托管多个 Session/View；默认关闭 |
| Scripting（扩展） | `init.js`、`termole.*` API、能力校验 | 绕过宿主能力 | 调用 SDK/IR；默认关闭 |

---

## 3. 聚合与不变量

### 3.1 Session（聚合根）

拥有：

- 子进程/进程树与 ConPTY；
- Terminal State（屏幕、回滚、模式、选择、标题）；
- 有界事件队列；
- 退出状态与排空状态。

不变量：

1. Session 可以没有 View；此时仍消费输出并保留有界历史。
2. 同一 Session 最多一个 View；重复 Attach 返回 `TERMOLE_STATUS_INVALID_STATE`。
3. 销毁 View 不终止 Session；独立版关闭窗口时由应用层先终止会话。
4. `terminate` 幂等；终止后 Session 仍可查询最后画面，直到销毁。
5. 退出码只在子进程结束且输出排空后可用；运行中返回 `INVALID_STATE`。
6. 事件队列有界；响铃/标题洪泛不能无限增长。
7. I/O 线程不调用宿主 UI；事件由宿主在 UI 线程调度。
8. 宿主进程退出不保留会话；首版不做后台保活。

### 3.2 View

1. 由宿主在 UI 线程创建、操作、销毁；子 HWND 布局由宿主负责。
2. 调用必须在拥有父 HWND 的 UI 线程。
3. 外观/滚动/选择/复制/粘贴不改变 Session 寿命。
4. 渲染只消费 Frame；不解析 VT。
5. 选择坐标是视图坐标（viewport + scrollback），与 Frame 提供的行身份对应。

### 3.3 Terminal State / Frame

1. Frame 是只读投影，带行列、单元格、光标、dirty、选择、模式摘要。
2. Frame 在 `frame_end` 或下一次更新后失效；渲染不得跨帧持有指针。
3. 宽字符、组合字符、Emoji 以字素单元存储；选择/复制/绘制使用同一单元模型。
4. 回滚有界（首版 1000 行）；alt screen 与主屏状态分离。
5. OSC 52 的剪贴板写入被忽略；终端输出不能直接写系统剪贴板。

### 3.4 Configuration / Profile / Window Definition

1. 优先级：默认值 → 配置文件 → 选中 Profile → （扩展）`init.js` → CLI。
2. CLI 参数错误在读取配置前失败（退出码 2）。
3. 配置读取失败：显示文件/行号，使用默认值继续；不阻塞启动。
4. Profile 是可复用值对象；窗口定义是 Profile 的可选子对象（扩展）。
5. 窗口定义解析失败按配置错误处理，不产生半初始化窗口。
6. 热重载只影响后续窗口或显式应用；不中断现有会话（扩展，需 ADR）。

### 3.5 Launcher / GUI 角色

1. `termole.exe` 是控制台 launcher；`--gui` 是内部角色，不公开。
2. 普通模式：启动 GUI 后立即返回 0，不等待窗口关闭。
3. `-e` / `--stdio`：等待 GUI/程序结束并转交退出码。
4. 退出码：程序码原样；关窗 130；启动失败 1；用法错误 2；基础设施 125。
5. `--stdio` 要求调用方 stdin/stdout 是管道/文件；控制台时返回 2。
6. 控制台隐藏：仅当 launcher 独占新控制台时隐藏；共享控制台不动。
7. `-e` 不转发调用方 stdio；`--stdio` 只转发 stdin/stdout，不解析内容。
8. 不实现 picker 专用错误码或选择结果协议。

### 3.6 Embedding SDK

1. 公共 ABI 为 C；不暴露 STL、C++ 异常、VT 引擎、D2D 类型。
2. 结构带 size/abi_version；版本不匹配显式失败。
3. 输入只在调用期借用；库异步保留时复制。
4. 库分配字符串由 `termole_free` 释放。
5. 错误分类稳定；不传播 HRESULT/系统错误为唯一契约。
6. C++ 包装只做 RAII/类型/结果转换。

### 3.7 Script Runtime（扩展）

1. 默认不执行脚本；无 `init.js` 时行为与现状一致。
2. 脚本只能使用 Host Capability 授予的操作；能力只收紧不扩大。
3. 终端输出（OSC/DSR 等）永远不能授予或触发脚本能力。
4. 默认无网络/通用文件 API；剪贴板/管道需显式授予。
5. 脚本失败回退 TOML/默认值并报诊断，不阻塞启动。
6. 引擎可选构建；关闭时相关 API 返回 `UNSUPPORTED`。

### 3.8 Server / Client（扩展）

1. 默认单进程；Server 只在显式 `--server` 时存在。
2. **不新增第二个可执行文件**：Client 角色由同一 `termole.exe` 的内部角色承担，以保持 ADR 0007 的“一 exe + 一 dll”交付约束。
3. Client 等待窗口/子进程结束并返回退出码；不转发 stdout。
4. `-e` / `--stdio` 强制单进程，不经过 Server。
5. 管道仅当前用户可访问；协议有版本与长度上限。
6. Server 崩溃由 Client 报错；默认不自动重启。
7. 多窗口共享字体/字形缓存/配置/JS；每窗口独立渲染线程；I/O 多路复用风险必须记录。

> 注意：v4 草案中的 `termoleclient.exe` 与 ADR 0007 冲突。
> 本模型改为同一 exe 的 Client 角色；若坚持独立客户端，需要新 ADR 并修改交付约束。

---

## 4. 状态机

### 4.1 Session

```text
Created -> Running -> Draining -> Exited
   |          |          |
   +----------+----------+--> Terminated (terminate 幂等)
                              |
                              +--> Destroyed（最后一个 View 销毁后）
```

- Running：子进程运行，消费输出。
- Draining：子进程已退出，排空 PTY；仍然可读最后画面。
- Exited：退出码可查询。
- Terminated：请求终止；可查询最后画面。

### 4.2 View

```text
Created -> Attached -> Destroyed
```

- Attached 依赖 Session 存活；Session 销毁前必须无 View。
- Destroyed 后句柄不可用；排队事件不得复活句柄。

### 4.3 Launcher

```text
Parse -> SpawnGUI -> Return0（普通模式）
                  \-> Wait -> ExitCode（-e/--stdio）
Parse -> UsageError(2)
SpawnGUI -> SpawnError(1)
GUI -> InfrastructureError(125)
```

### 4.4 Server / Client（扩展）

```text
Server: Stopped -> Starting -> Ready -> Draining -> Stopped
Client: Connecting -> Attached -> Waiting -> Exited
Client -> Error（握手/版本/ACL/Server 崩溃）
```

### 4.5 Script Runtime（扩展）

```text
Disabled -> Loading -> Ready
                    \-> Failed（回退配置，不阻塞）
```

---

## 5. 命令与领域事件

### 5.1 命令

| 命令 | 作用 | 发起方 | 线程 |
| --- | --- | --- | --- |
| CreateSession | 创建会话与 ConPTY | Launcher/宿主 | 任意（库内同步） |
| TerminateSession | 终止进程树，幂等 | 宿主/应用 | 任意 |
| Resize | 改字符网格与 PTY | 宿主/View | 任意 |
| WriteInput | 写原始字节到 PTY | 宿主/View | 任意 |
| SendKey | 按终端模式编码按键 | 宿主/View | 任意 |
| CreateView | 创建子 HWND | 宿主 | UI 线程 |
| DestroyView | 销毁视图，不终止会话 | 宿主 | UI 线程 |
| ApplyAppearance | 字体/主题/DPI | 宿主 | UI 线程 |
| Scroll / Select / Copy / Paste | 视图交互 | 宿主 | UI 线程 |
| ReloadConfig | 重载配置/窗口定义 | 应用（扩展） | UI 线程 |
| EvalScript | 执行本地脚本（扩展） | 应用 | 脚本线程/UI 约定 |

### 5.2 事件

| 事件 | 载荷 | 交付 |
| --- | --- | --- |
| TitleChanged | 新标题（UTF-8） | 宿主队列，UI 线程调度 |
| Bell | 无/原因 | 同上 |
| Exited | 退出码 | 排空后 |
| Error | 稳定错误类别 + 诊断 | 同上 |
| ConfigReloaded（扩展） | 新配置摘要 | UI 线程 |
| WindowOpened（扩展） | 窗口标识 | UI 线程 |

事件队列不携带裸 Session/View 指针；句柄销毁后事件不可复用。

---

## 6. 验收示例（Given/When/Then）

测试 ID 前缀 `DM-`；每条对应后续测试套件。

### 6.1 Session / View

**DM-SESSION-01 无 View 继续消费**
Given 创建 Session 且未创建 View；
When 子进程输出若干行；
Then 输出被消费，回滚有界保留；
When 创建 View；
Then 当前画面与历史可显示。

**DM-SESSION-02 重复 Attach**
Given Session 已有 View；
When 再次 CreateView；
Then 返回 `TERMOLE_STATUS_INVALID_STATE`。

**DM-SESSION-03 销毁 View 不终止会话**
Given Session 有 View 且程序运行中；
When DestroyView；
Then Session 仍 Running，继续消费输出。

**DM-SESSION-04 终止与退出码**
Given 程序运行中；
When TerminateSession 两次；
Then 两次均成功/幂等；
Then 退出码在排空后才可读。

### 6.2 Launcher / CLI

**DM-LAUNCH-01 普通模式**
Given `termole` 无 `-e/--stdio`；
When 启动；
Then launcher 启动 GUI 后立即返回 0。

**DM-LAUNCH-02 `-e` 等待**
Given `termole -e <program>`；
When 程序退出码 N；
Then launcher 返回 N；不转发调用方 stdio。

**DM-LAUNCH-03 关窗取消**
Given `-e` 等待中；
When 用户关闭窗口；
Then 返回 130。

**DM-LAUNCH-04 错误码**
Given 程序无法启动 / 用法错误 / 基础设施失败；
Then 分别返回 1 / 2 / 125。

**DM-LAUNCH-05 `--stdio` 前置条件**
Given 调用方 stdin 或 stdout 是控制台；
When `--stdio`；
Then 返回 2，不启动程序。

**DM-LAUNCH-06 `--stdio` 字节语义**
Given stdin/stdout 均为重定向；
When 程序读到/写出字节；
Then 不追加换行、不转码、不排序、不缓冲；退出码转交。

**DM-LAUNCH-07 控制台隐藏**
Given launcher 独占新控制台；
Then 启动 GUI 前后隐藏并释放新控制台；
Given 控制台属于调用方；
Then 不隐藏。

### 6.3 Configuration

**DM-CONFIG-01 优先级**
Given 默认值、配置文件、Profile、CLI 同时设置某键；
Then 生效顺序为默认→文件→Profile→CLI。

**DM-CONFIG-02 配置错误**
Given 配置含未知键/类型错误/未闭合值；
Then 输出文件与行号，继续用默认值启动。

**DM-CONFIG-03 CLI 先失败**
Given CLI 参数非法且配置文件也非法；
Then CLI 用法错误先返回 2，不读配置。

### 6.4 VT / Frame

**DM-VT-01 文本与光标**：打印、换行、回车、光标移动、擦除。
**DM-VT-02 SGR**：16 色、256 色、真彩、粗体/暗色/斜体/下划线/反显/隐藏。
**DM-VT-03 宽字符/组合/Emoji**：CJK 双宽、组合字、ZWJ、变体选择符。
**DM-VT-04 备用屏幕**：47/1047/1049 进入/退出，主屏状态恢复。
**DM-VT-05 回滚**：超过 1000 行后最旧行淘汰，内存有界。
**DM-VT-06 鼠标**：1000/1002/1003、SGR 1006、X10 编码。
**DM-VT-07 Bracketed paste**：2004 模式边界。
**DM-VT-08 查询与安全**：DSR/DA 响应写回；OSC 标题；BEL 事件；OSC 52 被拒绝。

### 6.5 Embedding ABI

**DM-ABI-01 版本/尺寸**
Given 调用方传入错误 size 或 abi_version；
Then 返回 `ABI_MISMATCH`，不读写后续字段。

**DM-ABI-02 所有权**
Given 库返回字符串；
Then 只能由 `termole_free` 释放；跨运行时释放属未定义。

**DM-ABI-03 线程**
Given 在非 UI 线程调用 View API；
Then 行为按契约明确（当前不保证/应拒绝）。

**DM-ABI-04 C-only 宿主**
Given 只用 C 编译器；
When 链接并调用 v3 ABI；
Then 可创建/查询/销毁。

### 6.6 扩展（需 ADR）

**DM-EXT-01 Server/Client**：同一 exe 的 Server/Client 角色；Client 返回子进程码；`--stdio` 强制单进程；Server 崩溃有明确错误。
**DM-EXT-02 Window Definition**：优先级、Win10/Win11 回退、热重载不中断会话。
**DM-EXT-03 Script Capability**：无 `init.js` 行为不变；允许写会话、拒绝剪贴板；OSC 不授予能力；JS 失败回退。

---

## 7. 能力/权限模型（脚本与扩展）

| 能力 | 作用域 | 默认 | 说明 |
| --- | --- | --- | --- |
| `session.write` | 单个 Session | 允许（本地配置） | 写入用户输入 |
| `session.key` | 单个 Session | 允许 | 发送按键 |
| `session.resize` | 单个 Session | 允许 | 调整尺寸 |
| `session.terminate` | 单个 Session | 允许 | 终止会话 |
| `screen.read` | 单个 Session | 允许 | 读可见文本/快照 |
| `selection.read` | 单个 View | 允许 | 读选区 |
| `clipboard.read` | 单个 View | 拒绝 | 需显式授权 |
| `clipboard.write` | 单个 View | 拒绝 | 需显式授权 |
| `pipe.run` | 单个 Session | 拒绝 | 外部命令 |
| `window.manage` | 单个 Server/Client | 拒绝 | 开窗/改窗口定义 |

规则：

1. 脚本只能收紧，不能扩大宿主授予的能力。
2. 终端输出不能触发能力授予或脚本执行。
3. 能力位随 ABI 版本化；未知位返回 `UNSUPPORTED`。

---

## 8. 追溯矩阵

| 来源 | 领域条目 | 测试套件 | 实现模块（计划） |
| --- | --- | --- | --- |
| ADR 0001 | DM-SESSION-01/02/03、DM-ABI-01/02 | `contract`、`host_api_test` | `api`、`session` |
| ADR 0002 | DM-EXT-03、能力模型 | `script` | `script`、`api` |
| ADR 0006 | DM-LAUNCH-02/05/06 | `characterization`、`t26_*` | `apps`、`session` |
| ADR 0007 | DM-LAUNCH-01/03/04/07 | `t26_launcher`、`cli_pipelines` | `apps` |
| config.md | DM-CONFIG-01/02/03 | `config_test`、`cli` | `apps` |
| vt-core.md | DM-VT-01..08 | `terminal_unicode_test`、`wrap_test`、`visual` | `vt`、`render` |
| host-api.md | DM-ABI-01..04 | `host_api_test`、`sdk-host` | `api` |
| 本次扩展 | DM-EXT-01/02/03 | `server`、`gui`、`script` | `client`、`ui`、`script` |

---

## 9. 开放问题与 ADR 草案

| 编号 | 问题 | 影响 | 建议 |
| --- | --- | --- | --- |
| Q1 | Server/Client 是否允许第二个 `termoleclient.exe`？ | 违反 ADR 0007 “一 exe + 一 dll” | **保持一 exe**：Client 作为内部角色；如需独立客户端，先改 ADR 0007 |
| Q2 | QuickJS 是否改变“配置不执行脚本”的既有语义？ | 与 config.md 第 61 行冲突 | ADR 0009：脚本为可选增量，默认关闭；无 `init.js` 行为不变 |
| Q3 | 窗口定义是新增配置键还是独立文件？ | 配置兼容性 | 先作为 `[window]`/Profile 子键，新增 ADR 0010 |
| Q4 | Server 模式默认是否自动启动？ | 启动行为/安全 | 默认显式 `--server`；`auto` 作为后续选项 |
| Q5 | 脚本默认权限是否允许写会话？ | 安全边界 | 本地配置可信；允许会话写，拒绝文件/网络/剪贴板 |
| Q6 | 130 取消码与子程序自身 130 冲突 | 已记录风险 | 保持现状，不新增专用码 |
| Q7 | 启动预算 120/40 ms 是否作为验收硬指标？ | 性能验收 | 先测基线，再固定阈值；不静默放宽 |

ADR 草案：

- **ADR 0008**：libghostty-vt 1.0.1 + MinGW + C 核心（实现决策，不改领域语义）。
- **ADR 0009**：QuickJS 脚本与宿主能力（增量，默认关闭）。
- **ADR 0010**：窗口定义与 DWM 外观（增量配置键）。
- **ADR 0011**：Server/Client 模式（同一 exe 的内部角色）。

---

## 10. TDD 策略

- 每个 `DM-*` 先写成失败测试，再写最小实现；测试命名使用领域术语。
- 测试标签：`domain`、`characterization`、`contract`、`adapter`、`integration`、`gui`、`perf`、`script`。
- 迁移前先跑绿现有 C++ 测试与 characterization；新实现按 C 测试逐条替换，旧测试在对应 C 测试绿之前不删除。
- 每个阶段结束时：`ctest` 全绿、ABI 不变、C 占比/边界门禁通过。
- GUI 结论必须截图/像素证据；不得用 `screen_text`/`paint_count` 代替。
- 领域模型评审通过后才开始 Phase 1 编码。

---

## 11. 变更记录

| 日期 | 变更 | 说明 |
| --- | --- | --- |
| 2026-11-19 | 初稿 | 领域建模，待评审 |

