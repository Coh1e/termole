# ADR 0006：终端与外部程序的数据通道

状态：已采纳（2026-10-31）。

外部程序负责交互并定义结果。Termole 提供终端窗口和数据通道。例如，PowerShell 提供候选，fzf 在窗口中接受选择，脚本从 stdout 读取 fzf 写出的结果。

| 入口 | 数据流向 |
| --- | --- |
| `-e` | 外部程序的 stdin、stdout、stderr 均连接终端。调用方等待并取得退出状态。 |
| `--stdio` | 调用方通过管道或文件向程序的 stdin 送入字节，从程序的 stdout 原样取得字节。交互界面和程序 stderr 走终端，Termole 自身的诊断写入调用方 stderr。调用方等待并取得退出状态。 |
| Pipe 动作（兼容） | `pipe-visible`、`pipe-selected`、`pipe-scrollback` 将视口、选区或回滚文本以 UTF-8 单向送入外部命令的 stdin。命令的 stdout、stderr 丢弃。 |

公开用法见 [CLI 契约](../cli.md)，等待规则见 [ADR 0007](0007-console-launcher-wait.md)。

## 决策依据

foot 1.28.0 在子进程启动时把 stdin、stdout 和 stderr 都接到 PTY 从端。其 `-e` 是 xterm 兼容参数。`footclient` 的等待模式回传退出状态，启动协议没有传递调用方标准流文件描述符。foot 的 `pipe-*` 则把终端文本单向送给外部程序。这些行为取自固定提交 `152487e` 的源码和手册。Termole 用 `--stdio` 为脚本显式提供候选输入和结果输出。[PTY 重定向](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/slave.c#L277) · [`-e` 手册](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/doc/foot.1.scd#L163) · [客户端协议](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/client-protocol.h#L14) · [退出状态](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/server.c#L96) · [`pipe-*` 实现](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/input.c#L239)
