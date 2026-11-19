# ADR 0013：跨平台目标下的实现与语言配比

- 状态：accepted
- 日期：2026-11-20
- 相关：ADR 0008、ADR 0012、ADR 0011
- 取代/修正：ADR 0012 中“尽量 C 化 GUI”的倾向

## 背景

产品目标新增“跨平台”。同时实践表明：终端外壳的输入语义、IME、选择、剪贴板、焦点、窗口编排用 C++ 更易维护；C 的优势在稳定 ABI、无 COM/STL 的协议/核心层。

“C 占比”本身不是目标；跨平台与可验证性优先。

## 决策

### 语言与层

| 层 | 语言 | 跨平台策略 |
| --- | --- | --- |
| VT 解析/终端状态 | C（libghostty-vt + 适配） | libghostty-vt 多平台；保持 C ABI |
| Snapshot/模式/选择文本 ABI | C | 无平台类型 |
| PTY/进程/平台服务 | C++ 接口 + 平台后端 | Windows ConPTY；POSIX `forkpty` 等后续 |
| View/输入/IME/选择/剪贴板/焦点 | **C++** | 保留为终端外壳主要实现 |
| Renderer | **C++ 接口** | Windows 后端 D2D/GDI；后续评估 SDL3/Skia/Metal/D3D |
| CLI/config/launcher/app | C++ | 现有实现 |
| server/client 协议 | C 编解码 + C++ 传输/生命周期 | 命名管道→后续 Unix socket |
| QuickJS | C ABI + 可选引擎 | 多平台 |

### 目标配比

- 不设“C 行数必须占 X%”的硬指标。
- 预期稳态：C 约 30–45%（VT/ABI/协议/脚本/PTY 原语），C++ 约 55–70%（外壳/平台/渲染/应用）。
- 新增 GUI/输入逻辑默认 C++；只有满足 ADR 0012 的 C 条件才放 C。

### 跨平台边界

新增/规划内部接口：

- `platform/pty`：spawn/resize/write/read/exit
- `platform/window`：窗口、DPI、焦点、IME、剪贴板
- `platform/renderer`：单元格绘制、字体度量、光标/选择
- `platform/paths`：配置/状态目录

首个跨平台里程碑：

1. 抽出上述接口，Windows 实现保持行为；
2. Linux：posix PTY + X11/Wayland 窗口（或 SDL3）+ Skia/OpenGL renderer；
3. macOS：PTY + Cocoa/Metal 后续。

## C GUI 的定位

`--c-gui` 保留为实验/嵌入路径，不再是默认切换目标；不为它继续重写 C++ View。C GUI 相关构建在 `TERMOLE_BUILD_C_GUI=OFF` 或非 Windows 时排除。

## 后果

- 默认 `--gui` 继续 C++ View；`--c-gui` 可选。
- 下一步优先抽平台接口与 server/client，而不是 C 比例。
- 现有 Windows 测试保持为回归基线；跨平台构建矩阵后续加入 CI。

## 验证

- Windows 全量测试（除已知 fzf 截图抖动）保持通过。
- `TERMOLE_BUILD_C_GUI=OFF` 时产品与测试不依赖 C GUI 源文件。

## 静态链接约束（所有平台）

用户要求：**所有平台必须静态链接**。因此：

| 平台 | libghostty-vt | 运行库 |
| --- | --- | --- |
| Windows x86_64 | vendored `ghostty-vt-static.lib`（已接入） | `-static` + `-static-libgcc/-static-libstdc++` |
| Linux x86_64 | 无官方预编译；必须从固定源码用 Zig 构建静态库 | 优先 musl 静态，或明确接受 glibc 静态的限制 |
| macOS | 只有 XCFramework 静态库 | 静态链接系统外依赖；系统库仍动态 |

Linux 的静态 glibc 存在 NSS/dlopen 等运行时限制，不能假设“加 `-static` 就完全自包含”。若目标是无 glibc 依赖的单文件，优先 musl 工具链，并把该选择写入构建 presets。

## 本机测试环境

- WSL2 Ubuntu 22.04 可用：`cmake 3.22.1`、`gcc/g++ 11.4`。
- 项目要求 CMake ≥ 3.28；在 WSL 中需先提供更新的 CMake（不改变 Windows 构建）。
- WSL 先用于 POSIX PTY/平台接口测试；libghostty-vt Linux 静态库需从源码构建后再接入。
- MSYS2 playground（`D:\playground`）用于 Windows 静态构建与测试，不作为 Linux ABI 验证环境。

## 跨平台实施顺序（修订）

1. 抽 `platform/pty`、`platform/window`、`platform/renderer`、`platform/paths` 接口；Windows 后端行为不变。
2. WSL：实现/测试 POSIX PTY 后端（不依赖 libghostty）。
3. 在 WSL 固定 Zig/源码版本，构建 libghostty-vt Linux 静态库并接入 C core 测试。
4. Linux 窗口/renderer 后端；macOS 最后。
5. server/client 协议保持 C 编解码，传输层按平台替换。
## 已落地：POSIX PTY 原语（WSL 静态验证）

新增 `src/platform/posix_pty.h/.cpp`：

- `openpty` + `fork` + `setsid/TIOCSCTTY`
- `TIOCSWINSZ` resize
- master 读写、waitpid 退出码
- 工作目录与 argv

WSL2 Ubuntu 22.04 验证：

```bash
g++ -std=c++23 -static -static-libgcc -static-libstdc++ \
    -I src src/platform/posix_pty.cpp tests/posix_pty_test.cpp -lutil \
    -o /tmp/posix_pty_static
```

结果：

```
ELF 64-bit LSB executable, x86-64, ... statically linked
PASS: spawn posix pty
PASS: child output arrives
PASS: child exit code preserved
PASS: posix pty
```

该原语尚未接入 `NativeSession`（Windows ConPTY 仍是产品路径）；它是 Linux/Unix 后端的第一步。