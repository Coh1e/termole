# Windows fzf 数据通道验证

2026-10-30。本报告是可行性验证，不代表已交付 Termole 弹窗或嵌入 SDK。

## 实测环境

- Windows x64，系统内核版本 10.0.26300.0；本机 MSVC 19.51.36260.0 / Windows SDK 10.0.26100.0。本轮不构成 Windows 10 22H2 兼容验证。
- CMake 4.4.5、Ninja 1.13.2、C++23（MSVC `/std:c++latest`）。
- 用户已安装的未修改 fzf 0.74.5 (`c0a193e0`)。
- fzf.exe SHA-256：`7CC161C1FD600C747D4C79670F02B3E90DE3AC268192482CFD16140B3A82D1F8`。
- FetchContent 固定 WIL `a1c0cbe1d7065aef6333399d3e25608eacdffc0f`，仅用头文件资源管理和错误处理。

## 方法

ConPTY 建立流程借鉴微软 MIT EchoCon 示例。父进程创建 ConPTY 交互管道，另建匿名管道传候选和接收结果；通过显式继承句柄列表把数据端点交给附着 ConPTY 的小型 helper。helper 将 fzf 的 stdin/stdout 接到数据端点，stderr 保持控制台。fzf 自行打开 CONIN$/CONOUT$，无需修改 fzf。

验证程序持续排空交互和结果管道；画面出现目标项后，通过 ConPTY 输入发送 Enter/Escape。结果断言直接比较独立 stdout 捕获的字节和退出码，不从画面提取结果。Job Object 负责关闭及异常时清理整个进程树。

## 通过场景

| 场景 | 验证结果 |
| --- | --- |
| 单选 | 返回 `beta\n`，退出 0 |
| Escape 取消 | 空结果，退出 130 |
| 多选 | 完整返回两个选择项，保持 fzf 自身顺序 |
| NUL 输入输出 | 原样返回含空格项及 NUL 分隔符 |
| NUL 下的嵌入换行 | 原样返回 `line\nbreak\0` |
| 中文及 Emoji | UTF-8 结果逐字节一致 |
| 模拟关闭 | 终止 Job，空结果，退出 130 |
| 大输入 | 20,001 项，超过匿名管道容量，仍完成交互和结果返回 |

CTest `picker_channels` 全部通过。此前五场景版另连续通过三次；扩展八场景版通过一次。交互字节数因刷新时机变化，不作为稳定性能指标。读取结束错误 109 为管道对端正常关闭。

## 复现

从项目根目录运行，替换 fzf 路径。`build.cmd` 的默认 MSVC 路径对应本机；其他安装位置可通过子进程内的 `TERMOLE_VCVARS` 指定，不需改变全局环境。

```powershell
.\spikes\picker\build.cmd -DTERMOLE_FZF_EXECUTABLE=C:/path/to/fzf.exe
ctest --test-dir .local/build/picker --output-on-failure -V
```

原始 `.result` 和 `.vt` 证据保留在 `.local/build/picker/evidence`，不提交。该验证没有渲染窗口，`close` 是进程树终止模拟，不代表已测试真实用户点击关闭按钮。

## 尚未验证

- 实际 Win32 窗口、鼠标、IME、DPI 与嵌入宿主。
- 发布用 GUI/CLI 入口从 CMD、PowerShell 的真实管道调用及退出等待。
- 上游预览/execute/become 等扩展动作、任意选择器、WSL fzf。
- 断开调用方管道、超大结果背压、诊断与显示分流、取消与基础设施错误的完整公开编码。
- helper 是否保留为正式内部组件；本次仅证明该通道方案可行。

## 来源

- [微软 EchoCon](https://github.com/microsoft/terminal/blob/95142e6ebfcbce21da27c3f7eb5b87e8abdb2555/samples/ConPTY/EchoCon/EchoCon/EchoCon.cpp)
- [fzf Windows renderer](https://github.com/junegunn/fzf/blob/c0a193e031f01fb50bf6f7be4381990a3753f7cf/src/tui/light_windows.go)
- [CreatePseudoConsole](https://learn.microsoft.com/en-us/windows/console/createpseudoconsole)
