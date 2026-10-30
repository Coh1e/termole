# Termole & Pip

Windows 原生、可嵌入的终端界面。C++23，CMake + Ninja，MIT。

首版仍在进行中，尚未交付完整 MVP。当前已实现 T01–T08、T10–T16 基础、T22/T23 的大部分切片：同一 Session/View 通过 `termole.dll` 在独立窗口和最小原生宿主中运行真实 PowerShell；CLI 与宿主可按逻辑 argv、工作目录和每会话环境启动任意目标程序；支持退出码与保留窗口、事件队列、1000 行回滚、备用屏幕、CJK/组合字符/Emoji、字体/主题/DPI、选择/复制/粘贴、鼠标模式/bracketed paste、Applet 窗口、TOML/Profile、独立 fzf picker、SDK 安装与便携包，并通过真实 fzf。

- [已确认设计决定](docs/design-decisions.md)
- [首版实施规格 v0.2 · GitHub Issue #1](https://github.com/Coh1e/termole/issues/1)
- [首版任务索引与阻塞关系](docs/issue-index.md)
- [CLI 用法与退出码](docs/cli.md)
- [T03–T13 中间切片验证](docs/t03-t13-validation.md)
- [TOML 配置与 Profile](docs/config.md)
- [T14 配置验证](docs/t14-validation.md)
- [SDK 与便携包](docs/sdk.md)
- [Picker 基础实现](docs/picker.md)
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

`build.cmd` 仅在子进程内载入 MSVC 环境；不同安装位置通过 `TERMOLE_VCVARS` 指定 `vcvars64.bat`。默认构建产品、测试和两个入口，并运行 CTest。`package.cmd` 额外安装开发包并生成 ZIP。

产物位于 `.local/build/windows/bin`：

| 文件 | 用途 |
| --- | --- |
| `termole.exe` | 独立终端窗口；支持 `--cwd`、`--env`、`--` 目标程序分隔符 |
| `termole-host.exe` | 最小原生宿主示例，展示宿主布局、焦点与生命周期 |
| `termole.dll` | C ABI（版本 2）与 C++23 包装所在组件 |
| `termole-picker.exe` | 独立 fzf picker：stdin 候选、stdout 结果、stderr 诊断 |
| `termole-test-helper.exe` | 测试用真实控制台子程序 |

`BUILD_TESTING=ON` 时另构建测试专用的 `termole-smoke.exe` / `termole-smoke-host.exe` 来驱动入口 smoke 测试；它们不编译进产品入口，也不属于发布包。

公开头文件为 `include/termole/termole.h`（C ABI）和 `include/termole/termole.hpp`（C++23 包装）。FetchContent 是项目唯一的依赖获取入口；产品目标当前没有 fetched 依赖，因此干净的 product configure 不访问网络。可选技术探针仍通过 FetchContent 获取固定源码，使用 `TERMOLE_PRESET=windows-spikes` 构建。

```powershell
$env:TERMOLE_PRESET = 'windows-spikes'
.\build.cmd
```

未找到 fzf 时只构建选择器探针、不注册选择器测试；Contour 比较探针默认关闭。下载源码、工具缓存、构建产物及原始运行证据均保留在项目 `.local`，不纳入版本控制。

## 当前边界

T01–T16 基础、T17 WSL 基础、T20/T21 初步、T22/T23 只承诺已列出的切片，不宣称完整终端兼容性。尚未完成：中文 IME 实机验收（T09）、嵌入 Termole 窗口的 picker/多选完整验收（T15/T16）、Neovim/Helix/lazygit 流程（T18/T19）、资源/性能 soak（T20/T21）和 Windows 10/11 发布验收（T24）。对应任务见 [任务索引](docs/issue-index.md)。

## 依赖与体积

产品代码只链接 Windows 系统组件（Kernel32、User32、GDI32、Imm32、Direct2D、DirectWrite；CLI 另用 Shell32 的 `CommandLineToArgvW`）和静态 CRT；`termole.dll`、`termole.exe`、`termole-host.exe` 不依赖 VC++ 运行库 DLL。Release 构建启用 LTO、`/OPT:REF`、`/OPT:ICF`、`/GR-`、`/Gy`、`/Gw` 与 `/Zc:inline`；产品入口不包含 smoke/`std::filesystem` 代码。当前 Release 体积：DLL 325,632 B、`termole.exe` 326,656 B、宿主 123,392 B；依赖和测试证据见 [T03–T13 验证报告](docs/t03-t13-validation.md) 与 [T22/T23 报告](docs/t22-t23-validation.md)。

第三方代码来源、许可证与产品/探针边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
