# 嵌入式 SDK（v0.1 / ABI 3）

Termole 以 `termole.dll` 提供 C ABI，并提供 C++23 头文件包装。宿主只消费公开头文件、导入库和 DLL，不需要产品内部源码。

## 文件

```text
bin/termole.dll
bin/termole.exe
lib/termole.lib
include/termole/termole.h
include/termole/termole.hpp
lib/cmake/termole/termoleConfig.cmake
share/termole/examples/sdk-host/
```

发布 ZIP 只有一个 DLL 和一个 EXE；`share/termole/examples/sdk-host` 是可独立构建的消费示例，不再附带额外宿主 EXE。

## 构建外部宿主

```cmake
cmake_minimum_required(VERSION 3.28)
project(MyTermoleHost LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(Termole CONFIG REQUIRED)
add_executable(my-host main.cpp)
target_link_libraries(my-host PRIVATE Termole::termole)
```

配置时把解压后的包的 `lib/cmake/termole` 加入 `CMAKE_PREFIX_PATH`。DLL 必须与宿主可执行文件一起分发，或位于加载路径。

## 契约

- `TERMOLE_ABI_VERSION` 当前为 3；结构体首字段为 `size`，`*_INIT` 宏设置完整大小。版本或大小不匹配时返回 `TERMOLE_STATUS_ABI_MISMATCH`。
- 所有 C 接口使用 `__cdecl`；任何失败通过 `termole_status` 和 `termole_last_error()` 返回，C++ 异常不跨 DLL 边界。
- `termole_session_options` 的 `stdin_handle` / `stdout_handle` 可选地把调用方标准句柄直接接到子进程（独立入口的 `--stdio` 数据通道）；两者必须同时设置，且与 `stdin_path` / `stdout_path` 互斥。子进程 stderr 与 UI 仍使用 ConPTY。
- `termole_session*` 与 `termole_view*` 是库拥有的不透明句柄。`termole_session_destroy` 释放调用方句柄；若 View 仍持有 Session，Session 继续存活，直到 View 销毁或显式 terminate。
- 一个 Session 同时最多一个 View。View 是父窗口的子 HWND；宿主负责显示、布局、焦点和 DPI 上下文。
- Session 的 I/O 在库线程；View 创建、销毁、外观、滚动、选择和输入转发必须在宿主 UI 线程调用。事件通过 `termole_session_poll_event` 由 UI 线程消费，库不回调宿主 UI。
- 字符串返回值（屏幕、历史、选区、事件文本）由库分配，使用 `termole_free` 释放；空结果返回空字符串或 `TERMOLE_EVENT_NONE`。
- `termole_session_get_exit_code` 只在 Session 停止后有效；`TERMOLE_STATUS_INVALID_STATE` 表示目标仍在运行。
- 环境变量数组为 `KEY=VALUE`；库复制后使用，不修改调用方环境。参数数组保留逻辑参数，不拼接 Shell 命令。
- 默认不读取用户 TOML；宿主通过公开选项自行传配置。终端输出不会自动写系统剪贴板。

## 生命周期

1. `termole_session_create` → 可选 `termole_view_create`。
2. 主窗口消息循环中把按键/字符/尺寸/焦点转发给 View（子窗口自动接收）。
3. 通过事件、屏幕/历史查询、退出码和选区接口读取状态。
4. `termole_view_destroy` 释放控件；Session 可继续运行或重附加新 View。
5. `termole_session_terminate` 结束进程树；`termole_session_destroy` 释放句柄。

ABI 冻结范围、示例源码和错误规则随 0.1 开发包发布；不兼容变更必须提升 `TERMOLE_ABI_VERSION`。
