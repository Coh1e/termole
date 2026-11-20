# 嵌入式 SDK（v0.1 / ABI 3）

状态：v0.1 的 ABI 3 已冻结（T22，[#23](https://github.com/Coh1e/termole/issues/23)）。
历史 ABI 验证见 [t22-validation.md](t22-validation.md)。当前 SDK 通过
`t22_sdk_consumer` 从安装包构建和运行独立宿主。宿主使用公开头文件和静态库。

## 文件

```text
bin/termole.exe
lib/termole.a
lib/ghostty-vt-static.lib
licenses/libghostty-vt.txt
include/termole/termole.h
include/termole/termole.hpp
lib/cmake/termole/termoleConfig.cmake
share/termole/examples/sdk-host/
```

`bin/termole.exe` 是独立入口。嵌入宿主通过 `Termole::termole` 链接安装包的静态库。

## 构建外部宿主

```cmake
cmake_minimum_required(VERSION 3.28)
project(MyTermoleHost LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(Termole CONFIG REQUIRED)
add_executable(my-host main.cpp)
target_link_libraries(my-host PRIVATE Termole::termole)
```

把解压后的包根目录加入 `CMAKE_PREFIX_PATH`，使用与 SDK 兼容的 MinGW-w64 编译器。
`examples/sdk-host` 是可独立配置、编译的最小示例；`tests/sdk-consumer` 是覆盖全部
宿主能力的验收宿主，两者都只使用公开头文件。

## ABI 规则（冻结）

| 项 | v0.1 规则 |
| --- | --- |
| 版本 | `TERMOLE_ABI_VERSION` 为 `3`；`termole_abi_version()` 返回库的 ABI 版本。 |
| 调用约定 | 所有导出函数使用 `TERMOLE_CALL`（`__cdecl`）；仅支持 Windows x64。 |
| 结构体布局 | `termole_session_options`、`termole_view_options`、`termole_appearance` 首字段为 `size`，第二个字段为 `abi_version`；调用方必须用 `TERMOLE_*_INIT` 初始化。 |
| 版本拒绝 | `size` 小于库编译时的结构体大小，或 `abi_version` 不等于 `TERMOLE_ABI_VERSION`，返回 `TERMOLE_STATUS_ABI_MISMATCH`，不解释其余字段。更大的结构体被接受，库只读取自己已知的前缀。 |
| 事件结构 | `termole_event.size` 必须至少为 `sizeof(termole_event)`；否则返回 `TERMOLE_STATUS_ABI_MISMATCH`。事件没有独立的 `abi_version`，类型由 `TERMOLE_EVENT_*` 冻结。 |
| 异常 | 所有可失败入口返回 `termole_status`。库将内部 C++ 异常转换为状态和诊断。 |
| 诊断 | `termole_last_error()` 返回当前线程的 UTF-8 诊断，非 NULL，有效期到同一线程下一次 Termole 调用。 |

## 所有权

- 输入缓冲区只在调用期间借用；库需要异步保留时复制，不保存宿主临时指针。
- 库返回的字符串（屏幕、历史、选区、事件文本、标题）由库分配，必须以
  `termole_free` 释放；`termole_free(NULL)` 合法。返回大小不含结尾 NUL。
- `termole_session` / `termole_view` 是库拥有的不透明句柄。`termole_session_destroy`
  释放调用方句柄；`termole_view_destroy` 释放 View。
- View 持有 Session 的引用：只要 View 存活，销毁调用方的 Session 句柄不会释放
  Session。宿主应先销毁 View，或先调用 `termole_session_terminate`。
- View 是父 HWND 的子窗口；父窗口归宿主所有。宿主负责布局、显示、焦点、DPI 上下文
  和最终销毁顺序。

## 线程规则

- Session 可在任意线程创建。除 `termole_session_destroy` 不得与同一句柄上的其他调用
  竞争外，Session 操作内部同步，可从任意线程调用。
- View 的创建、销毁、外观、滚动、选择、复制粘贴、焦点和输入转发必须在拥有父 HWND
  的 UI 线程调用。
- 事件通过 `termole_session_poll_event` 由 UI 线程消费；库从 I/O 线程不调用宿主
  回调，也不直接进入宿主 UI。
- 库不要求宿主运行额外的消息循环线程；View 使用宿主现有的消息循环。

## 生命周期

1. `termole_session_create` → 可选 `termole_view_create`。
2. 主窗口消息循环把尺寸、焦点、键盘、字符、鼠标消息交给子 View（或由宿主直接转发）。
3. 通过事件、屏幕/历史查询、选区、标题、尺寸和退出码读取状态。
4. `termole_view_destroy` 释放控件；Session 继续消费输出并保留有界历史。之后可
   `termole_view_create` 重新附加同一个 Session。
5. `termole_session_terminate` 结束本会话进程树；`termole_session_destroy` 释放句柄。
   若 View 仍持有 Session，Session 继续存活到 View 销毁或显式 terminate。

一个 Session 同时最多一个 View；重复附加返回 `TERMOLE_STATUS_INVALID_STATE`。
退出码只在 Session 停止后可用；运行中查询返回 `TERMOLE_STATUS_INVALID_STATE`。

## 错误

| 状态 | 含义 |
| --- | --- |
| `TERMOLE_STATUS_INVALID_ARGUMENT` | 空指针、越界网格、互斥选项等参数错误。 |
| `TERMOLE_STATUS_INVALID_STATE` | 对象当前状态不允许该操作，例如运行中取退出码、重复附加 View。 |
| `TERMOLE_STATUS_ABI_MISMATCH` | 版本或结构体大小不兼容。 |
| `TERMOLE_STATUS_SYSTEM_ERROR` | Windows 调用失败；诊断含细节。 |
| `TERMOLE_STATUS_NOT_FOUND` | 目标程序或默认 Shell 未找到。 |
| `TERMOLE_STATUS_OUT_OF_MEMORY` | 分配失败。 |
| `TERMOLE_STATUS_UNSUPPORTED` | 保留给当前版本不支持的能力。 |
| `TERMOLE_STATUS_EMPTY` | 事件队列为空。 |

库只报告结构化错误，不弹窗。独立版和宿主分别决定如何展示错误。

## 能力面

| 能力 | 公开接口 | 规格 |
| --- | --- | --- |
| 创建/终止 Session | `termole_session_create`、`termole_session_terminate`、`termole_session_destroy` | 38、40 |
| 布局与焦点 | View 是父 HWND 的子窗口；`termole_view_focus`；宿主自行 `MoveWindow`/DPI | 39 |
| View 寿命 | `termole_view_create`、`termole_view_destroy`；销毁不终止 Session，可重新附加 | 41、42 |
| 多个独立 Session/View | 每个 Session 独立；宿主可创建多组 | 43 |
| C ABI 与 C++23 包装 | `termole.h`、`termole.hpp`，包装复用同一 C ABI | 44 |
| ABI 检查 | `TERMOLE_ABI_VERSION`、结构体 `size`/`abi_version` | 45 |
| 分配规则 | `termole_free`；借用/复制规则 | 46 |
| 结构化错误 | `termole_status`、`termole_last_error` | 47 |
| 查询 | 尺寸、状态、标题、屏幕、历史、选区、退出码、cell metrics、paint count | 48 |
| 事件 | 标题、响铃、退出、错误；`termole_session_poll_event` | 49 |
| 不弹窗 | 库不创建对话框；错误由宿主展示 | 50 |
| 脚本复用边界 | 类型化操作/查询/事件契约；未来绑定只调用同一 C ABI | 51、52 |
| 示例、构建说明、能力文档 | `examples/sdk-host`、本文、`host-api.md` | 55 |

### 操作与查询

- 输入：`termole_session_write` 写入原始字节；`termole_session_send_key` 按当前终端
  模式编码一个键。粘贴与按键是不同意图；`termole_view_paste` 走宿主剪贴板并按
  bracketed paste 编码。
- 尺寸：`termole_session_resize` 同时改网格和 ConPTY；宿主移动 View 时 View 也会按
  客户区重算网格。
- 外观：`termole_view_set_appearance` 在 View 存活期间重建字体资源并应用主题，无效
  字体大小/主题返回 `INVALID_ARGUMENT`。
- 滚动/选择：`termole_view_scroll` 的负值向新输出滚动、正值向旧输出、0 回到实时屏幕；
  选择行是绝对文档行，历史从 0 开始。
- 剪贴板：`termole_view_copy_selection` 与 `termole_view_paste` 只在宿主显式调用时
  访问系统剪贴板；终端输出不会自动写剪贴板。
- 事件：`termole_session_poll_event` 返回 `TERMOLE_STATUS_EMPTY` 表示暂无事件；事件
  文本归库所有，用 `termole_free` 释放。队列有界（256），标题事件合并，退出事件保留。
- 历史：`TERMOLE_SCROLLBACK_LINES` 为 1000；`termole_session_get_scrollback_text`
  返回保留的最旧到最新行。

## 宿主责任与授权

- 启动参数由宿主显式传入 program、逻辑 argv、工作目录和 `KEY=VALUE` 环境数组；库不
  拼接 Shell 命令，也不修改调用方进程环境。
- 库不读取用户 TOML、不加载 Profile、不读取默认配置；嵌入宿主的设置全部来自公开
  选项。宿主可以自行读取配置，再传入选项。
- 库不创建顶层窗口、不接管宿主布局/焦点、不弹错误对话框、不安装全局钩子。
- 库不提供通用文件、网络或脚本 API。宿主通过句柄传入的 stdin/stdout 数据通道和
  显式剪贴板调用就是授权边界；终端输出不能扩大该边界。
- 进程树由 Session 管理：销毁 Session 或关闭独立窗口结束本会话进程树；不支持宿主
  退出后的会话保活。

## 数据通道

`termole_session_options` 的 `stdin_path` / `stdout_path` 或 `stdin_handle` /
`stdout_handle` 可把调用方的文件/句柄直接接到子进程；两组互斥，句柄必须成对设置。
子进程 stderr 和 UI 仍走 ConPTY。库会把句柄标记为可继承，但不替宿主解析、转码或
排序任何字节。该通道属于宿主自己的进程/管道组合，不是 launcher，也不是 picker 协议。

## 未来脚本能力

本文描述的类型化操作、查询和事件是未来脚本绑定的能力契约。引入脚本引擎时，绑定只
调用同一份 C ABI/核心校验逻辑，并由宿主授予会话、进程、剪贴板和资源范围；不提供
通用文件/网络扩展，也不自带解释器或自定义 VM。首版不接 QuickJS，不引入相关构建
依赖。

## 验证

`t22_sdk_consumer` 从当前构建安装一个开发包，用独立的 CMake 工程
`find_package(Termole CONFIG REQUIRED)` 编译 `tests/sdk-consumer`，再运行它驱动真实
CMD/ConPTY 和 View；同一测试还编译发布包内的 `examples/sdk-host`。像素证据和逐条
映射见 [t22-validation.md](t22-validation.md)。
