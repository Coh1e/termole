# ADR 0009：QuickJS 作为可选脚本引擎

- 状态：proposed
- 日期：2026-11-19
- 相关：ADR 0002、ADR 0008

## 背景

目标是把脚本能力做成“QuickJS 之于 Termole，类似 Lua 之于 Neovim”：配置、键位、窗口定义和启动逻辑可由用户脚本扩展，但默认终端必须保持轻量、快速启动。

当前默认构建不引入 QuickJS；`THIRD_PARTY_NOTICES.md` 已声明未随包。

## 决策

1. **默认构建不包含 QuickJS**；通过 CMake 选项 `TERMOLE_WITH_QUICKJS`（默认 OFF）启用。
2. 脚本接口走稳定 C ABI，不把 `JSValue`/`JSContext` 暴露给公共头文件：
   - `termole_script_create/destroy`
   - `termole_script_eval`
   - `termole_script_register`（宿主函数）
3. 脚本只通过宿主能力表访问 Session/View/窗口/剪贴板；不直接触碰 ghostty 句柄。
4. 启用时静态链接 QuickJS（与 MinGW 静态策略一致），并在 notices 记录版本、许可证与 SHA-256。
5. 脚本失败不得阻止终端启动：配置脚本错误降级为诊断，交互式脚本错误只影响对应命令。

## 后果

- 默认产物体积不变；启用后体积预算单独门禁。
- 需要新增脚本 ABI 测试、沙箱/超时测试和配置优先级测试。
- 在 C GUI 边界稳定前不实现脚本绑定，避免把 C++ 对象布局固化为 ABI。

## 未决

- QuickJS 版本与 `qjs` 兼容范围。
- 脚本生命周期（每 Session 一个 context，还是全局 host + per-session realm）。
- 是否提供 TypeScript 声明生成。

## 当前落地

`include/termole/script.h` 已提供稳定 C ABI：

- `termole_script_create`
- `termole_script_destroy`
- `termole_script_eval`

默认构建（QuickJS OFF）返回 `TERMOLE_STATUS_UNSUPPORTED`，不分配对象；
`tests/script_stub_test.c` 覆盖 null、ABI mismatch 与 unsupported 路径。

`TERMOLE_WITH_QUICKJS=ON` 时，Windows 静态 SDK 使用固定的 QuickJS 2026-06-04。
每个上下文有独立 runtime，限制为 16 MiB 堆、256 KiB 栈，JavaScript 执行约一秒后
请求中断。启用版测试覆盖异常诊断、循环中断、后续求值和 Release 体积。
当前入口供显式调用 SDK 的宿主使用。配置加载与 Session/View 能力绑定仍按本 ADR
的未决项推进。
