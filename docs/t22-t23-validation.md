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

`package.cmd` 重新构建、运行 CTest、安装并调用 CPack。产物：`.local/build/windows/package/Termole-0.1.0-win64.zip`。

内容：`bin/`（DLL、独立入口、picker、宿主示例）、`lib/`（导入库/CMake 包）、`include/`、`docs/`、`share/termole/examples`、README/LICENSE/THIRD_PARTY_NOTICES。未包含探针源码、`.local` 缓存、测试证据或 smoke 产品变体。

体积（Release，静态 CRT）：`termole.dll` 325,632 B、`termole.exe` 326,656 B、`termole-host.exe` 123,392 B、`termole-picker.exe` 241,664 B；无 VC++ 运行库 DLL。

## 限制

- 未在干净无开发工具机器上运行包内 `termole.exe`；只验证了安装布局、外部示例编译和本机运行。
- 运行包与开发包当前合并在一个 ZIP；任务要求可解释地提供两类内容，尚未拆分两个独立归档。
- 未生成签名、安装器或自动更新；T24 的 Windows 10/11 发布验收仍未执行。
