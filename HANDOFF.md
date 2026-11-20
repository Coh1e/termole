# Termole handoff — 2026-11-20

阶段性收尾。本文记录已完成、验证方式、未完成项与建议顺序。工作区改动均未提交。

## 1. 当前定位

- 项目：`D:\playground\home\dev\projects\termole`
- 产品目标：Windows 优先的终端，逐步跨平台；全平台静态链接优先。
- 语言边界（ADR 0012/0013）：
  - C：VT 适配、stable C ABI、协议/脚本 ABI、PTY 原语。
  - C++：终端外壳、输入/IME/选择/剪贴板、窗口编排、CLI/config、Windows View。
  - 不再为“C 比例”强行重写 C++ View。

## 2. 已完成

### 2.1 VT 核心

- vendored libghostty-vt 1.0.1（Windows gnu 静态库）。
- `include/termole/core.h` + `src/vt/termole_vt.c`：
  - frame/cell/宽字符/组合字符/样式/颜色
  - 模式（mouse/SGR/bracketed paste/application cursor/alt screen）
  - 标题、滚动、选择、纯文本
  - write_pty / bell / title 回调
- `NativeSession` 已切换到 C core；旧 `src/core/terminal.*` 删除。
- 回滚逻辑上限 1000 行（ghostty 后端 2000，适配层截取）。

### 2.2 Windows 构建

- MinGW-w64 gcc/g++ 16 + CMake **MSYS Makefiles**；无 Ninja/MSVC。
- `build.sh` 为唯一入口；`build.cmd` 已删除。
- 静态链接：
  - `-static`、`-static-libgcc -static-libstdc++`
  - 无 libstdc++/libgcc/winpthread/termole.dll 依赖
- Release `termole.exe` 约 5.96 MiB。
- 门禁：
  - `conventions_no_ninja`
  - `conventions_static_deps`
  - `conventions_c_core`
  - `conventions_release_size`

### 2.3 C GUI（实验角色）

- `include/termole/cview.h` + `src/gui/cview.c`
- `src/gui/gdi_render.c`：C GDI 渲染器
- `termole --c-gui`：
  - 窗口/字体/主题/cols/rows/title
  - `--program=` 与 `--` 参数
  - TOML `[window]` 注入
  - 选择、复制、粘贴、IME、鼠标上报、滚轮、resize 去抖
- 测试：
  - `cgui_smoke`
  - `cgui_product_smoke`
  - `cgui_product_options`
  - `cgui_product_program`
  - `cgui_visual`
  - `cgui_config_visual`
- 默认 `--gui` 仍是 C++ View；`--c-gui` 不设为默认。
- 曾尝试 `TERMOLE_C_GUI` launcher 路由，因 parity 未通过，已移除。

### 2.4 跨平台（Linux 静态）

- WSL2 Ubuntu 22.04 作为测试环境。
- Linux 静态 `libghostty-vt.a` 已构建并 vendored：
  - `third_party/libghostty-vt/1.0.1-linux/`
  - archive SHA-256：`c59b9938936f17c7543c3ebec1ddc6b800bace6ea67a38ba6027209348819ba5`
  - 源码 SHA-256：`333dfce97641ef9b536bb9e08907ad86abaaada50e8f7417b70d9284adef37aa`
- `src/platform/posix_pty.*`：openpty/fork/setsid/winsize/read/write/wait。
- `src/platform/posix_session.*`：读线程 + C core；write/resize/text/wait。
- `src/platform/pty.h` 统一 `Pty` 接口：
  - Linux：POSIX 后端
  - Windows：`src/platform/windows_pty.*`（ConPTY）
- `src/platform/windows_pty.*`：CreatePseudoConsole、CreateProcess、resize、read/write、job、wait。
- Linux 入口：
  - `linux/CMakeLists.txt`
  - `linux/CMakePresets.json`
  - `linux/README.md`
- Linux CTest（全静态）：
  - `vt_adapter_linux`
  - `posix_session_linux`
  - `posix_interactive_linux`（resize `stty size`、UTF-8、exit code）
- 链接警告：静态 glibc `getaddrinfo`；当前 core 不需要 DNS。

### 2.5 QuickJS

- `include/termole/script.h` 稳定 C ABI stub：
  - `termole_script_create/destroy/eval`
  - 默认返回 `UNSUPPORTED`
- 尚未引入 QuickJS 引擎。

### 2.6 文档

- ADR：0008（libghostty/MinGW/C core）、0009（QuickJS）、0010（窗口定义）、0011（server/client）、0012（C/C++ 边界）、0013（跨平台语言配比）。
- `docs/cross-platform-build.md`：Linux 源码/Zig/静态构建、校验和、验证命令。
- `docs/gui-c-renderer.md`：C renderer/C View/角色/截图测试。
- `README.md`、`THIRD_PARTY_NOTICES.md` 已更新。

### 2.7 测试基线

Windows（Debug，最近一次全量）：

```
100% tests passed out of 47
adapter/domain 9/9
characterization 17/17
visual 8/8
conventions 3/3
contract 3/3
WSL 1 skipped
```

Linux 静态：

```
3/3 passed
```

## 3. 未完成 / 已知问题

1. **默认 GUI 未切到 C View**
   - C GUI 明确可用，但没有达到 standalone parity；默认仍 C++ View。
   - `TERMOLE_C_GUI` 路由已移除。

2. **NativeSession 未迁移到 `platform::Pty`**
   - WindowsPty 已实现并测试，但 NativeSession 仍直接持有自己的 ConPTY、文件/handle 重定向和 stdio 数据通道。
   - 迁移需要保留重定向语义与全部 Windows 测试。

3. **Linux 窗口/renderer 缺失**
   - 只有 headless C core + PTY + 交互 Session。
   - WSL 当前没有 X11/SDL/Cairo/FreeType 开发库；Linux GUI 需要安装/静态化图形栈。

4. **macOS 未开始**
   - 需要 XCFramework 静态库 + Cocoa/Metal/Skia 后端。

5. **server/client 未实现**
   - ADR 0011 已定协议方向（同一 exe、命名管道、一个 Session 一个 View），尚无代码。

6. **QuickJS 只有 ABI stub**
   - 未 vendored QuickJS，未实现 `TERMOLE_WITH_QUICKJS`。

7. **视觉测试抖动**
   - fzf 场景曾偶发失败；已把关键 `pump_for` 等待提高到 800 ms，最近全量通过，但仍需观察 CI 稳定性。

8. **Linux 静态 glibc 警告**
   - `getaddrinfo` 需要运行时共享 glibc；若要求真正无动态依赖，需 musl 工具链。

9. **CMake 双入口**
   - 顶层仍是 Windows-only；Linux 用 `linux/CMakeLists.txt`。
   - 未来需合并为平台条件构建。

10. **配置/角色未统一**
    - `--c-gui` 自己解析参数；未完全复用 C++ TOML/CLI。
    - server/client 角色尚未进入 CLI。

## 4. 关键文件

```
include/termole/core.h              C VT ABI
include/termole/cview.h             C View ABI
include/termole/script.h            QuickJS ABI stub
src/vt/termole_vt.c                 C core adapter
src/session/session.{h,cpp}         Windows C++ Session
src/gui/gdi_render.c                C GDI renderer
src/gui/cview.c                     C View
src/platform/pty.h                  platform PTY contract
src/platform/posix_pty.*            POSIX PTY
src/platform/posix_session.*        POSIX Session
src/platform/windows_pty.*          Windows ConPTY backend
linux/                              Linux static core build
third_party/libghostty-vt/          Windows + Linux vendored deps
docs/adr/                           ADR 0001–0013
docs/cross-platform-build.md        Linux 构建与验证
docs/gui-c-renderer.md              C GUI 细节
```

## 5. 复现命令

### Windows

```bash
cd /home/dev/projects/termole
./build.sh --all
# 或
cmake --preset windows-mingw
cmake --build --preset windows-mingw --parallel
ctest --preset windows-mingw
```

Release：

```bash
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release --target termole_app --parallel
```

### Linux（WSL）

```bash
cd /mnt/d/playground/home/dev/projects/termole/linux
cmake --preset linux-static
cmake --build --preset linux-static --parallel
ctest --preset linux-static
```

## 6. 建议后续顺序

1. 把 `NativeSession` 的重定向/stdio 能力移入 `WindowsPty`，统一 `platform::Pty` 契约。
2. 抽共享 Session 契约，让 Windows/Linux 测试共用。
3. 实现 server/client（先 headless 协议与 Linux/POSIX，后 Windows 命名管道与 View attach）。
4. QuickJS 实际引擎 + 可选构建/体积门禁。
5. Linux 窗口/renderer（先选型：SDL3/Skia/X11；解决静态图形栈）。
6. macOS。
7. 统一 CMake 入口与 CI 矩阵。

## 7. 本次对话要点

- 从原 termole 迁移到 libghostty-vt C core，保留公共 C ABI。
- 完成 Windows 静态化、GNU Make、无 Ninja。
- 删除旧自研 VT，Session 接 C core。
- 补 ADR/门禁/体积预算。
- 做 C GUI 实验角色并验证截图，但最终判定“不为 C 而 C”。
- 明确跨平台目标，Linux 静态 C core/PTY/Session 已跑通。
- 用户要求阶段性结束，因此本文作为 handoff；未完成项见第 3 节。
