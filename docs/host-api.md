# 宿主能力契约（v0.1 / ABI 3）

状态：v0.1 的 ABI 3 已冻结（T22）。实现、完整规则和验证入口见
[sdk.md](sdk.md) 与 [t22-validation.md](t22-validation.md)。本文描述能力边界；
字段级布局和错误码以 [termole.h](../include/termole/termole.h) 为准。

## 统一契约

这里的 IR 是终端领域的类型化操作、查询和事件，不是序列化协议、字节码或通用执行
语言。原生 C++ 调用、C ABI 和未来脚本绑定最终进入同一份校验与执行逻辑，不各自
实现业务规则。

| 族 | 主要内容 | 约束 |
| --- | --- | --- |
| 会话操作 | CreateSession、TerminateSession | 程序、参数、工作目录、环境变量分别传递；不自行拼 Shell 管道 |
| 终端操作 | SendText、SendKey、Resize | 粘贴与按键是不同意图，由核心按当前终端模式编码 |
| 视图操作 | CreateView、DestroyView、ApplyAppearance、Scroll、Select、Copy、Paste | 在宿主 UI 线程执行；子窗口布局由宿主负责 |
| 查询 | State、Size、Title、Selection、ExitCode | 快照有明确所有权，退出码在退出完成前不可用 |
| 事件 | TitleChanged、Bell、Exited、Error | 通过宿主可调度队列交付，不在 I/O 线程调用宿主 UI |

独立版和嵌入宿主复用同一 Session/View 实现。T01 的首批实现额外提供
`termole_session_get_screen_text`：返回可见屏幕的 UTF-8 快照，供宿主诊断、无障碍或
验收读取；它不是选择查询，也不暴露 VT 引擎内部状态。

T02 将 ABI 升级到 2：`termole_session_options` 分别接收 program、逻辑 argv 数组、
工作目录和 `KEY=VALUE` 环境数组。参数不经过字符串拼接，空参数与 Unicode 保真；
环境数组在调用方环境副本上覆盖，不修改调用方进程。T26 将 ABI 升级到 3，在选项末尾
增加可选的 `stdin_handle` / `stdout_handle`，供独立入口的 `--stdio` 数据通道直接
接线；两者与 `stdin_path` / `stdout_path` 互斥。ABI 3 是 v0.1 的冻结版本。

v0.3 的 `-e` / `--stdio` / pipe 动作属于独立入口和应用层组合：`--stdio` 是 CLI 显式
数据通道，pipe 动作把终端文本送到外部命令；两者都不解析选择结果。嵌入宿主使用既有
查询（选区、滚屏、退出码）或自己的进程/管道；库不替宿主启动业务命令，也不实现
picker 协议。嵌入宿主拿到的通道能力与所有权见 [sdk.md](sdk.md#数据通道)，不与 CLI 的
launcher 角色混为一谈。

CreateView 附加既有会话；同一会话已有 View 时返回明确错误。T01 已用每 Session 的
attach 标志强制该约束，重复附加返回 `TERMOLE_STATUS_INVALID_STATE`。DestroyView 不
结束会话；会话本身仍消费输出并保留有界历史。关闭独立窗口是应用层先终止会话再销毁
视图，不在基础 View 析构中隐藏进程终止策略。

## ABI 与数据所有权

- 导出 C 函数及不透明 context/session/view 句柄；不暴露 STL、C++ 异常、VT 引擎和
  Direct2D 内部类型。
- 所有导出函数使用 `__cdecl`。结构体首字段为 `size`，选项结构另有 `abi_version`；
  `size` 小于 DLL 编译时的值或版本不同即返回 `TERMOLE_STATUS_ABI_MISMATCH`。更大的
  结构体按前缀读取，提供向前兼容空间。
- 输入数据在调用期间有效；若操作需要异步保留，库负责复制，不能保留宿主临时指针。
- 返回的库分配数据通过 `termole_free` 归还库，宿主不得跨运行时直接释放。
- 错误分为参数/状态错误、系统错误、依赖错误；返回稳定类别及诊断，不把 HRESULT/
  系统错误数字当成唯一领域契约。`termole_last_error()` 是线程局部诊断。
- C++23 包装只承担 RAII、类型与结果转换，复用 C ABI 行为；未来脚本绑定也不穿透
  核心内部结构。
- 调用约定、线程和生命周期的完整规则见 [sdk.md](sdk.md)。

## 事件和销毁

宿主在 UI 线程调度事件分发。队列唤醒只表示有工作，不携带裸 Session/View 指针；
销毁后的句柄不能被排队事件重新使用。Exited 在主进程结束且该会话输出排空后报告。
实现使用有界队列（256），标题事件合并，退出事件保留；T05 覆盖事件队列、View 寿命
和销毁后的继续消费。宿主仍须按生命周期规则先销毁 View 或显式 terminate，再销毁
Session 句柄。

## 宿主授权

宿主决定布局、焦点、可见性和生命周期；库不创建顶层窗口、不读取用户配置、不弹窗、
不安装全局钩子。启动程序、参数、目录、环境、数据通道句柄和显式剪贴板调用由宿主
提供，这些就是本版本的资源与授权边界。未来脚本绑定只能调用同一契约，并由宿主限制
会话、进程和剪贴板权限；不提供通用文件或网络脚本 API。

## 未来脚本宿主能力

脚本上下文只接收宿主授予的操作和资源范围。既有会话引用不等于允许创建任意进程；
终端输出也不能为脚本授予能力。文件、网络不提供通用脚本 API；剪贴板访问沿用宿主
授权。

QuickJS 后续接入时才决定引擎版本、执行线程、事件回调、资源上限和中断机制。当前
不为尚未接入的引擎引入构建依赖，也不实现自定义解释器或 VM。
