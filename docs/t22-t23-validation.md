# T22/T23 SDK 与便携包验证

## 开发包

`cmake --install` 安装：

```text
bin/termole.dll
bin/termole.exe
bin/termole-host.exe
bin/termole-picker.exe
lib/termole.lib
include/termole/{termole.h,termole.hpp}
lib/cmake/termole/termoleConfig.cmake
lib/cmake/termole/termoleConfigVersion.cmake
docs/{cli,config,host-api,sdk}.md
share/termole/examples/sdk-host/
```

外部示例 `examples/sdk-host` 只 `find_package(Termole CONFIG REQUIRED)`、链接 `Termole::termole`，使用公开头文件；已在本机针对 `.local/package` 实际配置、编译、链接通过。示例是独立 WIN32 宿主，拥有顶层窗口和消息循环。

ABI 2 作为 0.1 冻结候选：结构体 `size`/`abi_version` 检查、C 调用约定、句柄所有权、View 单实例、UI/I/O 线程边界、字符串 `termole_free` 和事件队列规则见 [sdk.md](sdk.md)。不兼容字段/语义变更必须提升版本。

## 便携包

`package.cmd` 重新构建、运行 CTest、安装并调用 CPack。产物：

```text
.local/build/windows/package/Termole-0.1.0-win64-Runtime.zip
.local/build/windows/package/Termole-0.1.0-win64-Development.zip
```

Runtime：`bin/`（DLL、独立入口、picker、宿主示例）、docs、README/LICENSE/THIRD_PARTY_NOTICES。
Development：`include/`、`lib/`（导入库/CMake 包）、`share/termole/examples/`，并带 `bin/termole.dll` 以便导入目标可直接使用。

未包含探针源码、`.local` 缓存、测试证据或 smoke 产品变体。已把 Runtime 解压到项目 `.local` 并用 `powershell.exe -NoProfile -Command exit` 启动 `termole.exe --close-on-exit`，退出 0；Development 解压后外部示例重新配置/编译/链接通过。

体积（Release，静态 CRT）：`termole.dll` 326,656 B、`termole.exe` 338,944 B、`termole-host.exe` 123,392 B、`termole-picker.exe` 247,296 B；无 VC++ 运行库 DLL。

## 限制

- Runtime/Development 已拆分；但未在无 MSVC/SDK 的干净机器上运行，也未生成签名、安装器或自动更新。
- Development 带 DLL 是导入目标自包含的需要；仍把 Runtime 视为运行入口。
- 未生成符号服务器/调试符号；未做发布级许可审计。
- T24 的 Windows 10/11 发布验收仍未执行。
