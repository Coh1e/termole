# ADR 0011：server/client 同一可执行文件

- 状态：proposed
- 日期：2026-11-19
- 相关：ADR 0007、ADR 0010

## 背景

Termole 需要可复用宿主（server）与轻量客户端（client）两种角色，同时保持单入口发布。用户要求不因 server/client 增加第二个 exe（除非 ADR 0007 改变）。

## 决策

1. 同一个 `termole.exe` 通过角色选择承载两种模式：
   - 默认/`--gui`：启动窗口与 Session。
   - `--server [--pipe NAME]`：长驻宿主，拥有 Session 与 PTY，不创建 View。
   - `--client [--pipe NAME]`：连接 server，创建 View 并转发输入/尺寸/选择。
2. IPC 使用 Windows 命名管道（`\\.\pipe\termole-<user>-<name>`），消息为长度前缀的二进制帧；不引入网络端口。
3. 一个 Session 对应一个 View；server 可持有多个 Session，但每个 Session 同时最多 attach 一个 client View。
4. server 退出策略：最后一个 client 断开且没有 `--detach` 请求时，按配置终止子进程；`TERMOLE_EXIT_DETACHED=125` 保留。
5. 协议版本与 `termole.h` ABI 独立演进；版本不匹配返回明确状态，不崩溃。
6. 角色选择、pipe 名、超时可由配置/脚本覆盖（ADR 0010 优先级）。

## 后果

- 需要协议定义、帧测试、并发/重连测试、退出码测试与安全边界（同用户默认 ACL）。
- 产品仍只发布一个 exe；构建与体积门禁不变。
- 在实现前不引入 service/托盘/多窗口。

## 未决

- 是否允许跨用户/跨会话连接（默认否）。
- 是否需要 `--attach` 到已有 GUI 进程。
- 协议是否复用现有 host ABI 的事件队列编号。
