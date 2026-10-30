# C++ 终端核心技术验证

日期：2026-10-30。此文记录实际执行证据，尚未决定产品采用哪个核心。

## 结论

Contour 稳定版的终端状态、VT 解析和输入编码可以在本机通过 C++23 / MSVC / CMake / Ninja 构建，不需要 Qt、Zig、SSH、HarfBuzz 或 FreeType。代价是需要一层选择源码目录的构建适配，以及 GSL、boxed-cpp、reflection-cpp、libunicode 四个依赖。

用户最新原则：能直接消费的小模块优先复用；若复用带入过重的依赖链，先理解成熟方案，再在本项目实现所需部分。此验证不构成默认选择 Contour，不应为接入它扩大产品能力或依赖。

## 固定来源

| 来源 | 版本 / 提交 | 用途 |
|---|---|---|
| contour-terminal/contour | v0.6.3.8249 / 3aef6f7243f571fd4fda2642da190a244bb87a57 | 实际通过的核心 |
| contour-terminal/contour | 4918660e7e04e8d507fb45396ef36934835fd57b | 开发分支整项目消费的失败/依赖成本证据 |
| microsoft/GSL | v3.1.0 / 0f6dbc9e2915ef5c16830f3fa3565738de2a9230 | 头文件 |
| contour-terminal/boxed-cpp | v1.4.3 / 82b362155c4e80d274282b51f1e4750af27e471a | 类型封装头文件 |
| contour-terminal/reflection-cpp | v0.4.0 / 8a83b3259f1be01098f942d525bcead0cfd14a74 | 反射头文件 |
| contour-terminal/libunicode | v0.9.0 / 25648e888e2433604c3e6f392796910f761d2285 | Unicode 处理；其构建下载 Unicode 17 数据 |
| microsoft/terminal | 95142e6ebfcbce21da27c3f7eb5b87e8abdb2555 | 仅做头文件/依赖探针，未完成核心构建 |

本项目 .local/deps 保存来源，.local/build 保存构建，.local/cpm 保存首次上游配置下载的缓存；没有安装系统依赖或修改其他项目。Contour、boxed-cpp、reflection-cpp、libunicode 主仓许可为 Apache-2.0，GSL 和 Microsoft Terminal 为 MIT；正式分发前仍须核对选中文件及附带数据的许可并保留通知。

## 实际通过的最小构建

入口：spikes/core/minimal/CMakeLists.txt。当前使用已经下载的本地源码，通过 FetchContent 加入四个依赖，并使用 upstream 原始 CMake 构建 crispy、vtparser 和 vtbackend。vtpty 在探针里仅编译 upstream MockPty/MockViewPty 两个源文件，避免 Windows 上游 vtpty 的必需 Libssh2 查找及 OpenConsole 打包。未修改第三方库的运行逻辑。

先执行 spikes/core/bootstrap.ps1 准备五个固定提交；它只在缺失时下载，发现已有源码版本不符会停止，避免覆盖本地改动。已验证本机现有源码的提交检查分支；尚未清空缓存验证全新下载分支，因此不宣称干净机器一键通过。CMake 为五个来源声明固定提交 GIT_TAG，并设置 GIT_SUBMODULES 为空；Contour 使用不存在的 SOURCE_SUBDIR 阻止自动加入完整应用。默认缓存是本项目 .local/fetchcontent-core。build-minimal.cmd 用 FETCHCONTENT_SOURCE_DIR_* 复用已经下载的源码；直接配置不带这些 override 时会由 FetchContent 下载固定提交。libunicode 初次配置还需要网络下载 Unicode 17 数据。

执行（项目根目录）：

```powershell
& .\spikes\core\bootstrap.ps1
cmd /c spikes\core\build-minimal.cmd
```

工具：Visual Studio 2026 BuildTools，MSVC 19.51.36260.0；CMake 4.4.5；Ninja 1.13.2。脚本只在子进程内载入 vcvars64，不永久改变环境。

实际结果：77 步编译/链接完成；随后加强 CJK 断言后的增量编译及 CTest 再次通过，1/1 测试通过。日志：.local/core-minimal-build.log。

测试覆盖：

- VT 文本与 SGR 序列消费后屏幕文字为 hello red。
- 进入与离开 alternate screen。
- 字符输入 abc 编码后输出字节仍为 abc。
- UTF-8 中文进入屏幕，读取文字完全匹配，首字单元宽度为 2。

未覆盖：真实 ConPTY、鼠标/现代键盘协议完整性、滚屏与 reflow 压力、Combining/ZWJ/Emoji 完整性、线程同步、IME、字体回退、渲染、嵌入生命周期及性能基准。测试使用 upstream MockTerm，仅为 headless 探针，不应直接把测试类当成产品会话接口。

Release 探针 exe 为 4,162,560 字节（最初通过版本）；vtbackend.lib 40,136,684 字节，unicode.lib 16,021,896 字节。静态库体积不能视为实际运行内存或最终应用大小。

## 开发分支直接消费的边界

.local/core-whole-probe/CMakeLists.txt（失败探针归档）尝试将完整 Contour 开发分支作为 FetchContent 子项目，关闭 GUI、扩展与测试。第一次在版本提取失败：上游 Version.cmake 使用 CMAKE_SOURCE_DIR 查询 metainfo.xml，作为子项目没有对应文件。复制上游 metainfo.xml 到探针根目录后前进。

下一次被 yaml-cpp 0.8.0 的旧 cmake_minimum_required 阻断；设置 CMAKE_POLICY_VERSION_MINIMUM=3.5 后前进。随后即使 GUI=OFF，仍拉取 FreeType/HarfBuzz；源码还无条件查找 OpenSSL，Windows vtpty 还要求 Libssh2。根据用户新约束已停止整项目配置，不再沿此链继续扩张依赖。日志：.local/core-configure.log。

.local/core-whole-probe/metainfo.xml 是复制的上游元数据，保留仅为重现该失败路径，不是 Termole 产品元数据。

## Windows Terminal 探针

.local/terminal-probe-source 中归档的头文件探针 实际尝试在 C++23 CMake/Ninja 下包含上游 pch.h + Terminal.hpp。配置成功，编译在 LibraryIncludes.h 的 wil/com.h 缺失处停止。日志：.local/terminal-probe.log。

这只能证明直接消费入口还有依赖准备工作，不能证明 TerminalCore 无法移植。其原始 terminalcore-lib.vcxproj 还引用 types、buffer、renderer/base、parser、adapter、Atlas、MIDI，并导入 WinRT 相关构建和 ICoreSettings.idl。未继续补齐整个链，未声称完成 TerminalCore 构建。

## 后续决策所需证据

T01 的实际选择见 [ADR 0005](adr/0005-minimal-vt-core.md)：未消费 Contour/libghostty-vt，而是按公开规范实现满足切片的 VT 子集。以下是当时保留的判断依据。

可借鉴或选择性复用：Contour vtparser 的状态机、InputGenerator 的标准输入编码、Grid/Screen 的字符及滚屏模型；Microsoft Terminal 的 ConPTY 生命周期、DirectWrite/Atlas、IME 与输入路径。依赖仍太重时，应先解释这些实现的关键不变量，再实现明确需要的最小部分，保留出处和许可，不盲目逐行搬运内部架构。

应避免：为保留一个大组件而引入 SSH、工作区、远程服务、整套字体栈；或只因最小文本例子通过就宣称拥有完整终端兼容性。实现选择仍由上述测试证据和产品范围决定。
