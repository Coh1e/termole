# T22/T23 SDK 与单包验证

## 单包交付

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

历史 picker 曾内嵌到 `termole.exe --picker`，没有 `termole-picker.exe`；该方向已被 ADR 0006 取代。v0.3 的目标是同一 `termole.exe` 内的控制台 launcher / GUI 双角色、`-e` / `--stdio` / pipe 动作；`termole-host.exe` 已从发布包移除，宿主示例只保留源码。

## 验证

- 单包解压后 `bin\termole.exe` 启动，窗口在屏幕内（`t01_app_window`）。
- `termole.exe --picker --picker-arg --filter ...`（历史 picker）从 stdin 读候选、stdout 返回结果；无 helper exe。该证据只说明旧数据通道可行，不代表 v0.3 `--stdio` 已验收。
- Development 消费：解压同一 ZIP，外部示例用 `find_package(Termole CONFIG REQUIRED)` 配置、编译、链接通过。
- `.local/build/clean` 从空构建目录配置/编译/安装（`BUILD_TESTING=OFF`），安装后的 exe 启动退出 0。
- 当前 Release：`termole.dll` 326,656 B、`termole.exe` 331,776 B；无 VC++ 运行库 DLL。launcher / GUI 双角色改造后需重测体积与启动行为。

## 限制

- 未在无 MSVC/SDK 的干净机器上运行。
- 未签名、未生成安装器。
- 没有 Windows 10 验收。
- 头文件/导入库和 EXE/DLL 在同一包中，但二进制仍严格为一个 DLL + 一个 EXE；launcher 与 GUI 角色在同一 exe 内。
