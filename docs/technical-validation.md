# 技术验证计划

状态：用户已确认进入技术验证；首轮环境、C++ 核心及 Windows fzf 数据通道验证已执行，并已在其上完成 T01 双入口最小终端切片。尚未交付完整首版应用或 SDK。

## 当前结果

- [T02 任意目标程序启动](t02-validation.md)：CLI 与公开宿主接口按逻辑 argv、cwd 和每会话环境启动目标；真实控制台子程序逐项核对空格、引号、空参数、Unicode、带路径程序、环境隔离，并覆盖 CMake/CMD/PowerShell/并行实例/无控制台启动。CTest 11/11 通过并连续复跑 5 次。
- [T01 双入口最小终端](t01-validation.md)：同一 DLL 的 Session/View 在独立窗口和最小宿主中运行真实 Windows PowerShell；公开 C ABI/C++23 包装、子 HWND、输入、尺寸同步、VT 子集和进程树收尾均有实际测试证据。测试 3/3 通过并连续复跑 5 次；Windows PowerShell 回退路径已验证，PowerShell 7 优先路径未在本机端到端验证。
- [核心验证](core-validation.md)：Contour 的 headless 核心在 MSVC/C++23/CMake/Ninja 下构建并通过文字、VT、alternate screen、字符输入和 CJK 宽度用例；仍牵连四个库，不自动采用为产品依赖。T01 按 [ADR 0005](adr/0005-minimal-vt-core.md) 使用项目内最小 VT 子集，而不是消费 Contour/libghostty-vt。
- [选择器验证（历史）](picker-validation.md)：未修改 Windows fzf 通过八类真实 ConPTY/独立数据管道场景；该 picker 方向已被 ADR 0006 取代，只保留证据。
- [宿主接口草案](host-api.md)：已细化操作、查询、事件、所有权和未来脚本绑定边界；T01 已实现首批 C ABI 子集，完整 ABI 尚未冻结。
- [原生界面基础验证](native-validation.md)：原生子 HWND、DirectWrite/Direct2D render target、离屏文字输出已实测；外部快照寿命与子窗口分离。T01 已把该基础扩展为真实 Session/View。
- 尚未执行完整 terminal 兼容性、真实 IME/DPI、选择/剪贴板、便携包与全部 Windows 10/11 验收；不得把 T01 切片标为 MVP 完成。

## 统一入口验收

根目录 CMakeLists.txt / CMakePresets.json / build.cmd 已实测。默认 `windows` preset 构建产品、测试和一个入口（launcher / GUI 双角色），CTest 通过（T01/T02 既有项、T03–T21 新增项、真实 fzf/外部程序、WSL、Neovim/Helix/lazygit、soak/idle-paint、默认窗口屏幕内回归）。`windows-spikes` preset 在保留探针入口的同时构建产品，全部探针/产品测试通过（上述项、picker 八场景、原生基础）。

- [T03–T13 中间切片](t03-t13-validation.md)：退出码/保留窗口、事件队列、回滚与 View 重附加、备用屏幕/滚屏区、Unicode 宽度/组合、字体主题 DPI、选择/复制/粘贴/滚动、鼠标模式/bracketed paste、Applet 窗口，并以真实 fzf 验证输入与退出。CTest 16/16 通过。
- [T14 配置](t14-validation.md)：TOML 根/Profile/CLI 优先级、错误文件与行号、默认配置回退。`t14_cli_config` 通过。
- [外部程序边界](adr/0006-terminal-selector-boundary.md)：`-e` 对齐 foot 的 PTY-only execute；`--stdio` 是 Termole 显式 stdin/stdout 扩展；pipe-visible/selected/scrollback 为单向动作。旧 picker 文档仅保留历史。控制台 launcher / GUI 双角色与默认等待见 [ADR 0007](adr/0007-console-launcher-wait.md)、[T26 验证](t26-validation.md)：`t26_launcher` 与 `t26_cli_pipelines` 覆盖普通模式立即返回、`-e`/`--stdio` 等待与退出码、真实 CMD/PowerShell 管道字节、取消 `130`、控制台隐藏与保留；`-e`/`--stdio` 的字节、退出码、大输入和慢读矩阵见 [T16 验证](t16-validation.md)。
- [T22/T23 SDK 与单包](t22-t23-validation.md)：单 ZIP、只有一个 DLL/EXE（launcher 与 GUI 角色在同一 exe）、外部 `find_package` 宿主编译、CPack 许可/文档/示例检查；未在无开发工具机器验收。更早的独立验证曾默认构建 picker 与 native 两个探针，CTest 2/2 通过；另在 `.local/build/all-spikes` 显式开启核心比较探针并复用固定源码，77 步构建完成，CTest 3/3 通过（picker 八场景、原生基础、VT 核心）。实际执行了对应目录的 `cmake --build` 和 `ctest --test-dir`；不是仅配置成功。
- [T17 CMD/WSL](t17-validation.md)：CMD 既有用例；`t17_wsl` 通过已安装 WSL 执行 `sh -c`。IME/粘贴/尺寸/失败诊断仍不完整。
- [T20 资源](t20-resources.md)：1000 行历史、256 事件、5 并发 Session、View 重附加；soak/背压未完成。
- [T21 性能](t21-performance.md)：启动约 1.0 s、空闲 CPU 0 ms/5 s、working set 51.6 MB、50k 行 2.0 s；帧延迟未测。
- [T18/T19](t18-t19-validation.md)：Neovim 0.12.6、Helix 25.07.1、lazygit 0.66.2 的基础插入/保存/退出与 lazygit 面板绘制通过；IME/鼠标/resize/双入口/完整流程未执行。
- [T24](t24-acceptance.md)：Windows 11 部分自动验收；Windows 10/IME/无开发工具/双系统未执行，状态未完成。
- [像素护栏（#27）](visual-harness-validation.md)：`PrintWindow` 截图 + 宽容 ink 统计；默认套件包含确定性 alt-screen 视觉用例，真实 fzf 视觉用例按 fzf 探测注册并以 `known-red` 标签隔离（#26 修复前可复现红色，默认套件保持绿色）。

T01 构建在全新的 `.local/build/windows` 目录完成，不依赖探针源码缓存或网络下载。这是本机现有编译工具链的验证，未模拟干净联网机器、旧版 Windows 或无开发工具的发布环境。

## 目标与顺序

1. 核实本机 C++23、Windows SDK、CMake、Ninja 环境；工具和依赖尽量在项目内管理，不改全局环境。
2. 比较 C++ 终端核心候选的最小构建依赖闭包。优先检查 Contour vtbackend 可否去掉 Qt 独立消费，再检查 Windows Terminal TerminalCore 的 CMake 迁移成本。按可复用功能、自研缺口、维护成本选择，不只按解析器体积。用户补充：若引入成熟模块牵连更多非必要依赖，转为研究成熟方案并实现所需部分，不为集成扩大依赖范围；验证成本用于决策，不表示已选择该依赖。
3. 用未经修改的 Windows fzf 验证独立 stdin 候选、stdout 结果和 ConPTY 交互通道，检查 PowerShell/CMD 等待、退出、取消、单选、多选、NUL 及原始字节。禁止从终端画面提取结果。
4. 验证同一控件用于独立窗口及最小原生宿主；验证销毁 View 后 Session 继续运行、重建 View 恢复画面、有界滚屏及宿主退出清理。
5. 再扩展至已确认的兼容性用例与便携交付。不设尚未测量的性能承诺。

## 候选与来源

- [Contour 根构建](https://github.com/contour-terminal/contour/blob/master/CMakeLists.txt) 与 [vtbackend 构建](https://github.com/contour-terminal/contour/blob/master/src/vtbackend/CMakeLists.txt)：C++23、Apache-2.0，有静态库目标；独立消费和去 Qt 成本尚未验证。
- [Windows Terminal TerminalCore 构建](https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalCore/lib/terminalcore-lib.vcxproj)：C++、MIT；现有内部组件、MSBuild/生成步骤依赖需要评估，不是现成纯 Win32/CMake SDK。
- [libghostty-vt CMake 接入](https://github.com/ghostty-org/ghostty/blob/main/dist/cmake/README.md)：提供 C 接口但依赖 Zig 构建，不默认引入。参考 Ghostty 设计不等于使用其代码。
- [ConPTY 官方说明](https://learn.microsoft.com/en-us/windows/console/createpseudoconsole)：输出为 UTF-8/VT 显示流，不能视为选择器原始 stdout。
- [标准句柄](https://learn.microsoft.com/windows/console/getstdhandle) 与 [fzf 用法](https://github.com/junegunn/fzf#usage)：需要验证 GUI 启动/管道继承及 fzf 独立数据通道。

## 交付证据

记录固定依赖提交及许可、构建命令和结果、自研或移植范围、可运行示例、选择器输入输出与退出结果、失败项和替代方案。只有真实通过的项目标为已验证。
