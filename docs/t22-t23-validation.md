# T22/T23 SDK 与单包验证

状态：T22 已由 [t22-validation.md](t22-validation.md) 验收（2026-11-01）；T23 仍未
交付，本文只记录 T23 的包目标与已知限制，不构成 T23 完成声明。T22 的外部开发包
消费证据以 t22-validation.md 为准。

## T22 开发包消费

当前构建可用 `cmake --install` 产生开发前缀；`tests/run_sdk_consumer.ps1` 会安装到
临时前缀，校验以下文件并用独立 CMake 工程消费：

```text
include/termole/{termole.h,termole.hpp}
lib/termole.lib
lib/cmake/termole/*
bin/termole.dll
share/termole/examples/sdk-host/
docs/sdk.md、docs/host-api.md
```

外部宿主和包内示例都通过 `find_package(Termole CONFIG REQUIRED)` 配置；详细断言、
像素证据和未覆盖项见 [t22-validation.md](t22-validation.md)。

## T23 单包目标

`package.cmd` 生成唯一 ZIP：

```text
.local/build/windows/package/Termole-0.1.0-win64.zip
```

包内二进制只有：

```text
bin/termole.exe
bin/termole.dll
```

同时带 SDK 文件（不产生额外 EXE）：

```text
include/termole/{termole.h,termole.hpp}
lib/termole.lib
lib/cmake/termole/*
share/termole/examples/sdk-host/
docs/、README、LICENSE、THIRD_PARTY_NOTICES
```

历史 picker 曾内嵌到 `termole.exe --picker`，没有 `termole-picker.exe`；该方向已被
ADR 0006 取代。v0.3 的目标是同一 `termole.exe` 内的控制台 launcher / GUI 双角色、
`-e` / `--stdio` / pipe 动作；`termole-host.exe` 已从发布包移除，宿主示例只保留源码。

## T23 待验证

- 从空构建目录配置/编译/安装，并在解压后的单包上确认 `bin\termole.exe` 启动、窗口
  在屏幕内。
- Development 消费：解压同一 ZIP，外部示例用 `find_package(Termole CONFIG REQUIRED)`
  配置、编译、链接通过。
- 包内只有一个 DLL 和一个 EXE，无 VC++ 运行库 DLL；许可、文档和示例齐全。
- 干净构建、无开发工具机器、Windows 10 验收仍属于 T23/T24。

## 限制

- 未在无 MSVC/SDK 的干净机器上运行。
- 未签名、未生成安装器。
- 没有 Windows 10 验收。
- 当前 T22 证据来自 Windows 11 x64 和本机已加载的 MSVC 环境。
