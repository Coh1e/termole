# foot 的管道与终端数据通道核查

核查日期：2026-10-31。本文是源码研究笔记，不修改 Termole 的既定规格或 ADR。

## 核查范围与结论

直接读取 foot 官方 Codeberg 仓库，固定提交为 `152487e494993ad7fb5633a27c7d7ad0f8810ea5`；该提交的 `meson.build` 声明版本为 1.28.0。源码暂存于忽略目录 `.local/foot-research/`。本次没有 Linux/Wayland 运行环境，因此以下是源码路径核查，未作 foot 运行验收。

在此提交中，`ls | foot -e fzf` **没有将调用方 stdin/stdout 直接透传给 fzf 的实现**。foot 会给子进程创建新的伪终端，并无条件将子进程的 stdin、stdout、stderr 全部接到伪终端从端。不能根据 foot 是一个具有标准输入输出的进程，推断其子进程保留这些管道。

## 正常启动路径

1. [`terminal.c:1193`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/terminal.c#L1193) 使用 `posix_openpt` 创建 PTY master（指定 `--pty` 时打开既有 PTY）；[`terminal.c:1437–1445`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/terminal.c#L1437) 在普通启动模式调用 `slave_spawn`。
2. [`slave.c:378–390`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/slave.c#L378) 创建用于报告启动失败的管道，然后 `fork`。此管道不是候选列表或选择结果通道。
3. [`slave.c:220–246`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/slave.c#L220) 对 PTY 执行 `grantpt`、`unlockpt`，调用 `setsid`，打开 PTY slave，再以 `TIOCSCTTY` 设置为新会话的控制终端。
4. **决定性证据**：[`slave.c:277–279`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/slave.c#L277) 无条件执行 `dup2(pts, STDIN_FILENO)`、`dup2(pts, STDOUT_FILENO)`、`dup2(pts, STDERR_FILENO)`。没有按 `isatty` 判断并保留外部管道的分支。
5. [`slave.c:291–306`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/slave.c#L291) 最终执行用户指定程序。因此 `fzf` 的 fd 0/1/2 指向新 PTY，而不是外层 `ls` 管道和调用方结果管道。

`-e` 不启用额外转发模式。官方 [`doc/foot.1.scd:163–171`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/doc/foot.1.scd#L163) 说明它被忽略，仅为兼容 `xterm -e`。

## server / footclient 是否另有转发

[`client-protocol.h:14–31`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/client-protocol.h#L14) 的启动协议传送工作目录、激活 token、配置覆盖、参数和可选环境变量。客户端以普通 `send` 发送这些数据（[`client.c:56–76`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/client.c#L56)），没有以 `SCM_RIGHTS` 传递调用方标准文件描述符。

服务端解包后仍调用相同的 `term_init`（[`server.c:390–395`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/server.c#L390)），因而最终走同一 `slave_spawn` / `dup2` 路径。

footclient 确实会接收子进程退出码：[`server.c:96–103`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/server.c#L96) 写回一个整数，[`client.c:568–617`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/client.c#L568) 接收并作为自身退出状态返回。**退出状态返回不等同于 stdout 结果转发。**

## 相近但不同的实际能力

foot 的 `pipe-command-output` 将已经显示的命令输出交给另一个程序。官方文档示例先将输入保存为临时文件，再启动 `footclient emacsclient -nw` 编辑该文件，最后删除临时文件。见 [`doc/foot.1.scd:474–489`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/doc/foot.1.scd#L474)。这与外部 `stdin → 子进程 → stdout` 透传不是同一种能力。

实现上，[`input.c:239–335`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/input.c#L239) 将屏幕、滚屏、选择或上个命令的输出转换成文本，经新建管道送入外部程序的 stdin；该程序的 stdout 和 stderr 指向 `/dev/null`。这里也没有将外部程序的结果返回给 foot 的原始调用方。

## foot 的外部程序用例：pipe-*

foot 没有“picker 结果返回调用方”的能力，但有另一套通用外部程序集成：

- [`foot.ini.5.scd:1393–1415`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/doc/foot.ini.5.scd#L1393) 定义 `pipe-visible`、`pipe-scrollback`、`pipe-selected`、`pipe-command-output`：把当前可见文本、全部回滚、当前选择或上一条命令输出送到外部命令的 stdin。
- 外部命令的 stdout/stderr 指向 `/dev/null`，结果是**单向**的；命令内部可以再运行 fzf/fuzzel 等选择器，但选择结果由该命令自己消费，不回到 foot 的调用方。
- 示例 [`foot.ini.5.scd:1409`](https://codeberg.org/dnkl/foot/src/commit/152487e494993ad7fb5633a27c7d7ad0f8810ea5/doc/foot.ini.5.scd#L1409)：`pipe-visible=[sh -c "xurls | uniq | tac | fuzzel | xargs -r firefox"] Control+Print`。fuzzel 是 UI 选择器，但结果被 `xargs` 用来启动 firefox，而不是返回给调用 foot 的 shell。
- `spawn-terminal` 只新建终端；`--server`/`footclient` 只转发退出码，不转发 stdout。

因此 foot 支持的是“**终端文本 → 外部命令 → 命令内部的选择/动作**”，不支持“**调用方管道 → 终端 UI 选择器 → 调用方结果管道**”。`-e` 也不是后者的实现。

## 通过额外文件描述符实现所需组合

对于直接启动的 foot（不是跨 server 的 footclient），可由外层 shell 先将原 stdin/stdout 保存在额外文件描述符，再由 foot 内的 shell 恢复：

```sh
ls | foot -e sh -c 'fzf <&3 >&4' 3<&0 4>&1
```

源码推导的通道安排是：外层 `3<&0` 保存候选管道，`4>&1` 保存调用方结果出口；foot 只把子进程 fd 0/1/2 改接 PTY，子进程 shell 再通过 `<&3 >&4` 把 fzf 的 stdin/stdout 改接回数据通道。PTY 仍是控制终端。

fzf 的交互可以独立打开终端，见官方 [`src/tui/light_unix.go:42–69`](https://github.com/junegunn/fzf/blob/master/src/tui/light_unix.go#L42-L69) 的 `openTtyIn` / `openTtyOut`。因此键盘与界面可走新 PTY，候选与结果走保留的管道。这个具体命令仅经源码推导，**本次未实测**；不能将其写作 foot 原生的 stdin/stdout 透传选项，也不能套用于没有传送这些 FD 的 footclient 协议。

## 对 Termole 的可用结论

这份核查说明：**不能把 foot `-e` 当作“调用方 stdin/stdout 透传”的证据**。Termole 若要实现 `candidates | termole ...` 的选择器行为，必须在 Termole 层显式建立候选/结果数据通道，或由调用方 shell 用额外 FD/临时文件包装；不能声称与 foot `-e` 等价。

具体接口以修订后的 spec #1 v0.3 和 [ADR 0006](adr/0006-terminal-selector-boundary.md) 为准：`-e` 是 PTY-only，`--stdio` 是显式调用方数据通道，pipe-* 是单向动作。

若存在用户实际使用的 foot 版本、补丁、别名或包装脚本，需要针对那个具体入口继续核查；本笔记不声称检查了所有历史版本与下游补丁。
