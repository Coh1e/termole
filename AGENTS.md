# AGENTS.md

## Agent skills

### Issue tracker

规格和任务记录在 Coh1e/termole 的 GitHub Issues。优先使用 GitHub 插件，操作前读 [Issue 跟踪说明](docs/agents/issue-tracker.md)。

### Triage labels

标准分诊角色和项目的 `verify` 标签见[标签映射](docs/agents/triage-labels.md)。

### Domain docs

采用单一领域文档结构，阅读入口和规则见[领域文档说明](docs/agents/domain.md)。

## 仓库规则

- 信息查询只报告发现。改动限于约定范围，保留他人的修改，沿用仓库结构和工具，清理本次改动产生的无用代码。
- 缺少会改变结果的决定时询问用户。变更既有接口或已约定需求前先确认。发布、支出、修改访问权限或删除用户数据前取得授权，已有授权覆盖的操作继续执行。
- 技术选型优先跨平台 C。能用 C 实现的逻辑用 C，C++23/26 仅用于有明确语言优势的模块。保持 C 核心稳定，平台和语言差异留在可替换的外围。
- 构建与依赖集成统一用 CMake。禁止引入 vcpkg、Conan 或同类依赖管理器。

## 按任务阅读

- 产品定位或交付范围：读 [产品基线](README.md#产品基线)。定位其他文档时查 [文档索引](docs/README.md)。
- 定位一项行为的实现与测试，从[程序设计与架构](docs/architecture.md)进入。调整术语或设计决定时按上方 Domain docs 指引处理。
- 技术选型、核心 ABI、平台边界或新增依赖：读 [跨平台 C 核心与 CMake](docs/adr/0016-portable-c-core.md)。
- 修改或评审代码、更新行为声明：读 [编码标准](CODING_STANDARDS.md)。构建或测试前读 [构建说明](docs/cross-platform-build.md)和 [验证指南](docs/agents/testing.md)。
- 修改 View、渲染器、窗口或选择器行为：改动前读 [验证指南](docs/agents/testing.md)。界面可见性结论必须有截图或像素证据。

## 必做检查

- 修改行为时用现有框架补充测试，覆盖相关边界和失败路径。修复缺陷前尽可能复现。
- 最后一次代码改动后，在本地运行格式检查、受影响的构建和相关测试。根据改动范围选择[验证项目](docs/agents/testing.md#inner-loop)。当前 Windows 验收使用本机 Windows 11 x64，取消本地和远端 Windows 10 验证及 GitHub Actions CI。
- 内存或并发风险在工具支持时用 sanitizer 检查。性能改动记录同等条件下的工作负载、环境、基线和结果。
- 检查最终 diff 中的无关改动、敏感信息、生成物和兼容性变化。先报告结果，再列出实际完成的检查和剩余限制。验收证据齐全后才报告完成。
