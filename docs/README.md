# Termole 文档

| 要找什么 | 从这里开始 |
| --- | --- |
| 产品与平台承诺 | [产品基线](../README.md#产品基线) |
| 交付场景与验收 | [Windows TUI Applet](https://github.com/Coh1e/termole/issues/31) · [轻量独立终端](https://github.com/Coh1e/termole/issues/32) |
| 轻量终端与 Applet 的公开入口 | [CLI](cli.md) · [TOML 与 Profile](config.md) |
| 构建与贡献 | [跨平台构建](cross-platform-build.md) · [验证指南](agents/testing.md) · [平台入口](https://github.com/Coh1e/termole/tree/main/linux) · [仓库指引](https://github.com/Coh1e/termole/blob/main/AGENTS.md) · [编码标准](https://github.com/Coh1e/termole/blob/main/CODING_STANDARDS.md) |
| 术语和设计决定 | [术语表](../GLOSSARY.md) · [跨平台 C 核心与 CMake](adr/0016-portable-c-core.md) · [终端数据通道](adr/0006-terminal-selector-boundary.md) · [VT 核心与 View](adr/0008-libghostty-vt-mingw-c-core.md) · [ADR 索引](adr/README.md) |
| 问题与任务 | [GitHub Issues](https://github.com/Coh1e/termole/issues) · [跟踪器说明](https://github.com/Coh1e/termole/blob/main/docs/agents/issue-tracker.md) |
| 第三方依赖与许可 | [第三方声明](../THIRD_PARTY_NOTICES.md) |

## 兼容参考

- 进程内嵌入见 [SDK ABI 3](sdk.md)和 [示例构建说明](sdk.md#构建外部宿主)。
- 会话脱离与重连见[本地会话](cli.md#本地会话)。Pipe 动作见 [CLI](cli.md#pipe-动作)和[配置](config.md#pipe-动作按键)。已有脚本接口见 [SDK 可选脚本引擎](sdk.md#可选脚本引擎)。
