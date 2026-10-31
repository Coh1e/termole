# Termole & Pip

Windows 原生、可嵌入的终端界面。C++23，CMake + Ninja，MIT。

首版仍在进行中，尚未交付完整 MVP。当前已实现 T01–T08、T10–T19 基础、T20/T21 初步、T22/T23 的大部分切片、T26 控制台 launcher / GUI 双角色：同一 Session/View 通过 `termole.dll` 在独立窗口和最小原生宿主中运行真实 PowerShell；CLI 与宿主可按逻辑 argv、工作目录和每会话环境启动任意目标程序；支持退出码与保留窗口、事件队列、1000 行回滚、备用屏幕、CJK/组合字符/Emoji、字体/主题/DPI、选择/复制/粘贴、鼠标模式/bracketed paste、Applet 窗口、TOML/Profile、CMD/WSL/Neovim/Helix/lazygit 基础路径，以及 `termole.exe` + `termole.dll` 的单包交付。`termole.exe` 现在是控制台 launcher：普通模式启动窗口后立即返回，`-e` / `--stdio` 等待并转交程序退出状态；旧 picker 已移除。

- [已确认设计决定](docs/design-decisions.md)
- [首版实施规格 v0.3 · GitHub Issue #1](https://github.com/Coh1e/termole/issues/1)
- [首版实施规格 v0.3（本地副本）](docs/implementation-spec-v0.3.md)
- [首版任务索引与阻塞关系](docs/issue-index.md)
- [CLI 用法与退出码](docs/cli.md)
- [T03–T13 中间切片验证](docs/t03-t13-validation.md)
- [TOML 配置与 Profile](docs/config.md)
- [T14 配置验证](docs/t14-validation.md)
- [SDK 与便携包](docs/sdk.md)
- [外部程序边界（ADR 0006）](docs/adr/0006-terminal-selector-boundary.md)
- [控制台 launcher 与默认等待（ADR 0007）](docs/adr/0007-console-launcher-wait.md)
- [T26 launcher / 默认等待验证](docs/t26-validation.md)
- [Picker 历史（已取代）](docs/picker.md)
- [资源有界](docs/t20-resources.md)
- [性能基线](docs/t21-performance.md)
- [CMD/WSL 基础验证](docs/t17-validation.md)
- [编辑器/Git TUI 验证状态](docs/t18-t19-validation.md)
- [首版交付验收状态](docs/t24-acceptance.md)
- [T22/T23 验证](docs/t22-t23-validation.md)
- [T02 启动任意目标程序验证](docs/t02-validation.md)
- [T01 双入口验证报告](docs/t01-validation.md)
- [最小 VT 核心范围与来源](docs/vt-core.md)
- [领域术语](GLOSSARY.md)
- [宿主能力接口草案](docs/host-api.md)
- [技术验证计划](docs/technical-validation.md)
- [C++ 核心构建实测](docs/core-validation.md)
- [Windows fzf 通道实测](docs/picker-validation.md)
- [Windows 原生界面基础实测](docs/native-validation.md)
- [原始规格 v0.1](docs/product-spec-v0.1.md)（后续决定优先）

## 构建与运行

需要 Windows 10 22H2 / Windows 11 x64、MSVC 或 clang-cl、Windows SDK、CMake ≥ 3.28 和 Ninja。在开发者命令行中：

```powershell
.\build.cmd
```

`build.cmd` 仅在子进程内载入 MSVC 环境；不同安装位置通过 `TERMOLE_VCVARS` 指定 `vcvars64.bat`。默认构建产品、测试和一个入口（launcher / GUI 双角色），并运行 CTest。`package.cmd` 额外安装并生成单包 ZIP。

编辑内循环用 `scripts\test-one.cmd <target> <ctest-regex>`，例如 `scripts\test-one.cmd termole_wrap_test t17_wrap`。提交前再跑全量 `build.cmd`。一次性安装空白/冲突标记钩子：

```powershell
.\scripts\install-hooks.cmd
```

验证与视觉证据规则见 [docs/agents/testing.md](docs/agents/testing.md)。

产物位于 `.local/build/windows/bin`：

| 文件 | 用途 |
| --- | --- |
| `termole.exe` | 独立入口。控制台 launcher + 同一 exe 的内部 GUI 角色：普通模式启动窗口后立即返回；`-e` / `--stdio` 等待程序结束并转交退出码 |
| `termole.dll` | C ABI（版本 3）、C++23 包装和可嵌入 View 所在组件 |
| `termole-test-helper.exe` | 仅测试构建使用的真实控制台子程序 |

发布包只安装 `termole.exe` 和 `termole.dll` 两个二进制；头文件、导入库和 SDK 示例在同一 ZIP 中，供宿主编译使用。

`BUILD_TESTING=ON` 时另构建测试专用的 `termole-smoke.exe` / `termole-smoke-host.exe` 来驱动入口 smoke 测试；它们不编译进产品入口，也不属于发布包。

公开头文件为 `include/termole/termole.h`（C ABI）和 `include/termole/termole.hpp`（C++23 包装）。FetchContent 是项目唯一的依赖获取入口；产品目标当前没有 fetched 依赖，因此干净的 product configure 不访问网络。可选技术探针仍通过 FetchContent 获取固定源码，使用 `TERMOLE_PRESET=windows-spikes` 构建。

```powershell
$env:TERMOLE_PRESET = 'windows-spikes'
.\build.cmd
```

未找到 fzf 时只构建选择器探针、不注册选择器测试；Contour 比较探针默认关闭。下载源码、工具缓存、构建产物及原始运行证据均保留在项目 `.local`，不纳入版本控制。

## 当前边界

T01–T19 基础、T20/T21 初步、T22/T23/T26 只承诺已列出的切片，不宣称完整终端兼容性。尚未完成：中文 IME 实机验收（T09）、`-e`/`--stdio` 的完整 fzf UI（字节/退出码/背压矩阵已由 [T16 验证](docs/t16-validation.md) 覆盖）、pipe 动作（T25）、编辑器/lazygit 的 IME/鼠标/resize/双入口完整流程（T18/T19）、资源/性能 soak（T20/T21）和 Windows 10/11 发布验收（T24）。对应任务见 [任务索引](docs/issue-index.md)。

## 依赖与体积

产品代码只链接 Windows 系统组件（Kernel32、User32、GDI32、Imm32、Direct2D、DirectWrite）和静态 CRT；`termole.dll`、`termole.exe` 不依赖 VC++ 运行库 DLL。Release 构建启用 LTO、`/OPT:REF`、`/OPT:ICF`、`/GR-`、`/Gy`、`/Gw` 与 `/Zc:inline`；产品入口不包含 smoke/`std::filesystem` 代码。当前 Release 体积：DLL 327,168 B、`termole.exe` 344,064 B（launcher/GUI 改造后）。依赖和测试证据见 [T03–T13 验证报告](docs/t03-t13-validation.md)、[T22/T23 报告](docs/t22-t23-validation.md) 与 [T26 验证](docs/t26-validation.md)。

第三方代码来源、许可证与产品/探针边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
