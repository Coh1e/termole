# T22 验证：外部应用仅凭开发包嵌入终端

- 任务：[#23](https://github.com/Coh1e/termole/issues/23)
- 父规格：[#1](https://github.com/Coh1e/termole/issues/1) 用户故事 38–52、55
- 日期：2026-11-01（Asia/Shanghai）
- 环境：Windows 11 Pro N for Workstations 10.0.26300 x64；MSVC 14.51.36231
  （Visual Studio 2026 BuildTools）；CMake 4.4.5；Ninja 1.13.2；Windows
  PowerShell 5.1；Release

## 结论

外部 C++ 宿主只消费安装包中的 `include/termole/termole.h`、
`include/termole/termole.hpp`、`lib/termole.lib`、`lib/cmake/termole/*` 和
`bin/termole.dll`，在宿主自己的顶层窗口中嵌入真实 Session/View，并完成会话
创建/终止、View 寿命、输入、尺寸、外观、滚动选择、剪贴板、查询和事件。v0.1 的
ABI 3 已冻结；不兼容版本/结构体大小明确失败，C++ 异常不跨 C ABI。库不读取用户
配置、不弹窗、不创建额外顶层窗口。发布包内的 `examples/sdk-host` 也能只凭同一个
安装包配置、编译、链接。

本文只声称上表范围内的嵌入能力；不表示 Windows 10、干净无开发工具机器、
QuickJS 或完整 MVP 已验收。

## 本次改动

- `tests/sdk-consumer/`：独立 CMake 工程与验收宿主。工程用
  `find_package(Termole CONFIG REQUIRED PATHS <package> NO_DEFAULT_PATH)` 解析
  安装包，并断言公开 include 目录和导入 DLL 都在包内；宿主只包含
  `termole.hpp` 和 `capture.h`。
- `tests/run_sdk_consumer.ps1`：安装当前构建到临时前缀，校验包内文件，配置/编译
  独立宿主和包内 `examples/sdk-host`，在包内 DLL 加载路径下运行宿主并校验 PNG、
  `PrintWindow` 和 ink 统计。
- `tests/CMakeLists.txt`：注册 `t22_sdk_consumer`（`SKIP_RETURN_CODE 77`，仅在
  MSVC 环境未加载时跳过）。
- `examples/sdk-host/main.cpp`：宿主激活时把焦点交还 View；`WM_DESTROY` 兜底
  释放 View/Session。示例仍然只负责宿主自己的窗口、布局、焦点和关闭顺序。
- `examples/sdk-host/README.md`、`docs/sdk.md`、`docs/host-api.md`：冻结 ABI 3
  的版本、调用约定、所有权、线程、错误、能力面和宿主授权规则。
- `docs/evidence/t22-sdk-view.png`、`docs/evidence/t22-sdk-host.png`：本次运行
  的像素证据。

没有修改 `src/` 或 `include/`：本次验收所需的实现已经在既有 Session/View/ABI
切片中存在，本票补齐的是外部消费证明、示例完整性和冻结文档。

## 逐条验收

| 验收标准 | 证据 | 结果 |
| --- | --- | --- |
| 外部最小宿主仅消费公开头文件、链接库和 DLL，覆盖会话创建/终止、View 寿命、输入、尺寸、外观、滚动选择、剪贴板、查询和事件 | `t22_sdk_consumer` 从安装前缀配置独立工程；编译命令的 include 只有包内 `include`，导入库和 DLL 均来自包内。宿主运行真实 `cmd.exe`：创建/终止 Session、View 子 HWND 与重新附加、`WM_CHAR` 执行 `set /a 12345+67890` 得到 `80235`、两次宿主 resize 改变 ConPTY 网格、`set_appearance` 改变 cell metrics、滚动到 `line-0000`、选择/复制/清空、剪贴板粘贴执行命令、尺寸/标题/屏幕/历史/退出码查询、title/bell/exit 事件。运行日志 90 项断言全部 PASS，0 FAIL。 | 通过 |
| 冻结首版 ABI 版本、调用约定、所有权、线程与错误规则；不兼容版本拒绝，C++ 异常不跨边界 | `termole_abi_version()` 与 `TERMOLE_ABI_VERSION` 均为 3；`size` 小于结构体、`abi_version` 不匹配、事件 `size` 过小都返回 `TERMOLE_STATUS_ABI_MISMATCH` 且失败后句柄为空；空参数/空输出、运行中取退出码、终止空 Session 返回结构化状态；`termole_last_error()` 有诊断；库分配字符串由 `termole_free` 归还；Session 查询从工作线程执行 100 次成功。规则冻结在 `docs/sdk.md` 与 `docs/host-api.md`。 | 通过 |
| 宿主掌握布局、焦点、资源与授权范围，不强制加载用户配置、不擅自弹窗 | View 是宿主的 `WS_CHILD`，`GA_ROOT` 仍是宿主窗口；宿主 `MoveWindow` 改变网格；`termole_view_focus` 后 `GetFocus` 在 View；测试把 `APPDATA` 指向含非法 `Termole\config.toml` 的目录，Session 仍正常启动；缺失程序返回 `NOT_FOUND`；CBT 钩子未观察到 `#32770` 对话框；当前进程可见顶层窗口只有宿主。剪贴板只在 `copy_selection`/`paste` 显式调用时访问。 | 通过 |
| 提供可执行嵌入示例、构建说明和能力文档；原生能力契约可供未来脚本复用，但不接 QuickJS 或自定义 VM | 包内 `share/termole/examples/sdk-host` 在同一测试里用 `find_package` 配置、编译、链接成功；`examples/sdk-host/README.md`、`docs/sdk.md`、`docs/host-api.md` 给出构建、生命周期、线程、错误和能力面；`docs/sdk.md` 明确未来绑定只调用同一 C ABI，不引入 QuickJS/VM/通用文件网络 API。 | 通过 |

### 规格 38–52、55 映射

| 用户故事 | 证据 |
| --- | --- |
| 38 原生子窗口、39 宿主布局/焦点、40 Session/View 独立寿命 | View 父 HWND、`WS_CHILD`、`GA_ROOT`、宿主 resize、focus 断言 |
| 41 销毁 View 后 Session 继续、42 重新附加恢复画面 | `--delayed-lines`：销毁 View 后输出 `after-detach` 仍消费；重新附加后 viewport 可见 |
| 43 多组独立 Session/View | 同一宿主同时/顺序运行 CMD、history、events、detach 多个 Session |
| 44 C ABI 与 C++23 包装、45 ABI 拒绝、46 分配规则、47 结构化错误 | 独立工程只链接 `Termole::termole`；ABI/参数错误断言；`termole_free`；`termole_last_error` |
| 48 状态/尺寸/标题/选择/退出码查询 | `size`、`screen_text`、`scrollback_text`、`title`、`selection_text`、`exit_code`、`is_running`、cell metrics |
| 49 标题/响铃/退出/错误事件经队列 | `--events` 会话的 TitleChanged、Bell、Exited(code 7)；Error 路径见“未覆盖” |
| 50 库不弹错误对话框 | CBT 钩子 0 个 `#32770`；缺失程序只返回状态 |
| 51 原生契约供未来脚本、52 宿主授权 | `docs/sdk.md`/`host-api.md` 的契约与授权边界；无 QuickJS/VM |
| 55 最小宿主示例、构建说明、开发包 | 包内示例编译通过；`docs/sdk.md` 构建段落；`t22_sdk_consumer` 安装前缀 |

## 自动测试

```bat
scripts\test-one.cmd termole_cli_report_helper t22_sdk_consumer
```

等价的核心步骤：

```text
cmake --install .local\build\windows --prefix <build>\tests\sdk-consumer\package
cmake -S tests\sdk-consumer -B <build>\tests\sdk-consumer\consumer-build ^
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<package> ^
  -DTERMOLE_PACKAGE_ROOT=<package>
cmake --build <build>\tests\sdk-consumer\consumer-build
cmake -S <package>\share\termole\examples\sdk-host -B <example-build> ...
cmake --build <example-build>
<consumer> --helper <cli-report-helper.exe> --artifacts <artifacts> ^
  --expected-dll <package>\bin\termole.dll
```

本次实际结果：`ctest --preset windows -R t22_sdk_consumer` **1/1 通过，8.77 s**；
完整 `build.cmd` 的 CTest **40/40 通过，213.92 s**。宿主日志 **90 PASS / 0 FAIL /
0 SKIP**。宿主实际加载 `...\tests\sdk-consumer\package\bin\termole.dll`，不是构建树
中的 DLL。

### 像素证据

View 是真实的 `TermoleViewWindow`，宿主用
`PrintWindow(PW_RENDERFULLCONTENT)` 捕获并统计：

| 捕获 | 尺寸 | inkPixels | inkRatio | strongPixels | 文件 |
| --- | ---: | ---: | ---: | ---: | --- |
| View | 900×560 | 11,905 | 0.023621 | 10,682 | [t22-sdk-view.png](evidence/t22-sdk-view.png) |
| 宿主窗口 | 1010×690 | 159,783 | 0.229277 | 158,143 | [t22-sdk-host.png](evidence/t22-sdk-host.png) |

两图均可见 `SDK>` 提示符、`echo TERMOLE-SDK-HOST-OK` 输入行和实际输出
`TERMOLE-SDK-HOST-OK`；像素断言要求 ink ≥ 500、inkRatio ≥ 0.0008、strong ≥ 100。
截图是可见性证据；行为判定来自同一运行的 90 项断言。

## 未覆盖/限制

- 未在 Windows 10 22H2 上运行；本机是 Windows 11 10.0.26300 x64。
- 未在无 MSVC/SDK 的干净机器上配置、编译外部宿主；这属于 T23/T24 的干净构建/交付
  验收。`t22_sdk_consumer` 需要 `build.cmd` 已加载的 MSVC 环境，否则以 77 跳过。
- `TERMOLE_EVENT_ERROR` 事件路径没有故障注入；本次验证的是 TitleChanged、Bell、
  Exited。Error 事件的存在和队列规则来自实现与 T05 的队列测试，不是本票的注入证据。
- C ABI 只验证 Windows x64 的单一调用约定；没有 32 位目标。
- 没有注入真实的 `std::bad_alloc` 或任意 C++ 异常；验证的是公开失败路径返回
  `termole_status`、失败不泄漏句柄，以及 `guard` 的代码路径。C++ 异常不跨边界的
  规则由实现和代码审查确认，未做故障注入。
- 没有从错误的 UI 线程调用 View 来证明库会拒绝；线程规则是文档契约，Session 的
  跨线程查询已实测。
- 剪贴板用例在不可用剪贴板的环境会打印 SKIP 而非失败；本次运行可写、可读、可粘贴，
  没有跳过。
- 宿主的受控 VT/事件输入仍使用测试构建的 `cli-report-helper.exe`；它是测试程序，
  不在发布包中，也不被产品代码引用。
- 示例 `termole-sdk-host.exe` 在测试中完成配置、编译、链接；交互式窗口行为由独立
  验收宿主覆盖，没有单独对示例窗口做像素断言。
