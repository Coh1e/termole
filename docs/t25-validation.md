# T25 验证：foot 风格 pipe-visible / pipe-selected / pipe-scrollback

- 任务：[#28](https://github.com/Coh1e/termole/issues/28)
- 决策：[ADR 0006](adr/0006-terminal-selector-boundary.md)、[ADR 0007](adr/0007-console-launcher-wait.md)
- 日期：2026-11-01
- 环境：Windows 11 x64 本机；MSVC 14.51（VS 18 BuildTools）、CMake 4.4、Ninja；Release

## 交付形态

- 配置新增三个根级表：`[pipe.visible]`、`[pipe.selected]`、`[pipe.scrollback]`。每表必须有 `key` 和 `command`；没有表就不注册热键。配置形状与约束见 [config.md](config.md#pipe-动作按键)。
- 按键和弦：大小写不敏感，至少包含 `ctrl` 或 `alt`；支持字母、数字、F1–F24 和导航键；重复和弦、与内置复制/粘贴快捷键冲突、未知修饰键/基础键、未知动作、缺失或空命令、profile 内的 pipe 表都有带文件与行号的诊断。
- 命令执行：`%COMSPEC%`（缺失时 `cmd.exe`）以 `/d /s /c "<command>"` 运行，Termole 不解析命令语言。stdin 接管道，stdout/stderr 接 `NUL`；`CreateProcess` 用显式句柄列表，只继承管道读端和 `NUL`，不把 ConPTY/调用方句柄带进命令。
- 文本固定 UTF-8，行分隔符 `\n`，不追加尾随换行。`pipe-visible` 复用 View 视口（去行尾空格、去末尾空行），`pipe-selected` 复用选区矩形（保留空格），`pipe-scrollback` 复用 Session 保留的回滚（最多 1000 行）。
- 文本在后台线程写入管道，慢命令或不读 stdin 的命令不阻塞 UI；命令退出码不进入会话。只有 shell 进程无法启动时才通过现有诊断路径报错（`-e`/`--stdio` 写调用方 stderr，普通交互模式在无 stderr 时使用消息框）；命令内部失败（含工具缺失/语法错误）由命令退出码表示，Termole 不解析。
- 按键在独立入口的应用层拦截：有 pipe 绑定时给 View 子窗口安装子类过程，匹配的和弦消费 `WM_KEYDOWN` 及其后的 `WM_CHAR`/`WM_SYSCHAR`；其他消息原样交给 View。没有配置时不安装拦截，默认终端行为不变。嵌入宿主按约定不读取用户配置，也不注册 pipe 热键；它通过 [host-api.md](host-api.md) 的公开查询自行组合文本和命令，本次不新增 C ABI。三个文本来源仍由同一 View/Session 定义，独立入口与宿主查询的边界一致。

## 自动测试

`build.cmd`（configure + 全量构建 + 全量 CTest）：**39/39 通过**（本机最终一次 269.80 s）。

### `t14_cli_config`

在真实 `termole.exe --gui --config` 启动路径中验证配置契约：

- 合法 `[pipe.visible]` 表被接受，程序仍正常启动。
- `key = 'v'`（缺少 ctrl/alt）报 `invalid key binding`，且带文件名与行号。
- `ctrl+shift+v`（与内置粘贴冲突）报 `built-in`，且带文件名与行号。
- 两个动作使用同一和弦报 `already bound`，指向第二个表。
- 只有 `key` 没有 `command`、命令只有空格、未知动作 `[pipe.clipboard]`、未知基础键 `ctrl+shift+hyper`、`[profiles.work.pipe.visible]` 都有明确诊断；所有错误都按既有策略回退默认配置并继续启动。

### `t25_pipe_runner`

`tests/pipe_runner_test.cpp` + `pipe-helper.exe`，直接验证命令边界：

- `call "<helper>" --sink <文件> --exit-code 7` 经 `cmd.exe` 执行；命令收到逐字节一致的负载，覆盖 UTF-8 中文、CRLF、内嵌 NUL 和“没有尾随换行”。
- 空文本仍启动命令，命令立即读到 EOF，收到零字节。
- 不存在的 shell 返回失败并留下 `could not start` 诊断。
- 命令退出码 7 不产生启动失败。

### `t25_pipe_e2e`（20 项断言）

`tests/pipe_e2e_test.cpp` 启动真实 `termole.exe -e pipe-helper.exe --content 30`，使用临时 TOML 配置，向真实 View 子窗口投递 `WM_KEYDOWN`/`WM_CHAR`/鼠标消息：

- 受控程序输出 30 行可预测文本；测试用同字体/DPI 的探针 View 测量真实网格，再按网格计算期望值，不假设 CLI 的 `--columns/--rows` 等于最终单元数。本次实测为 79×9，因此视口是第 22–30 行，回滚是第 1–21 行。
- `pipe-visible` 收到视口的精确 UTF-8 文本：行间 `\n`、无尾随换行、无行尾空格。
- `pipe-selected` 收到从视口左上到右下的完整矩形：每行补到 79 列（保留空格），行间 `\n`，无尾随换行。
- `pipe-scrollback` 收到有界回滚的精确文本。
- 首个命令以 `call` 内建命令启动，证明使用 Windows shell；其退出码为 7，但窗口仍在运行，会话不受影响。
- 和弦的 `WM_CHAR` 被消费；随后普通 `Z` 仍到达受控程序的 VT 输入日志（`input.bin` 恰好是 `Z`），证明没有吞掉后续输入。
- 命令向 stdout/stderr 写 `PIPE-SINK-*` 噪声；调用方 stdout 为空，stderr 不含这些字节，说明不污染调用方、不进入终端。

### 像素证据

`PrintWindow(PW_RENDERFULLCONTENT)` 捕获真实 `termole.exe` 窗口，断言颜色数 ≥ 8 且同时存在暗/亮像素；成功捕获写入
[t25-pipe.png](evidence/t25-pipe.png)（1077×337，系统浅色主题，显示第 22–30 行）。测试产物目录同时保留 BMP、三次命令收到的文本和输入日志。

## 构建产物

- `termole.exe`：379,904 B（新增 pipe 配置、按键路由与命令启动）
- `termole.dll`：327,680 B

仍是 1 个 `termole.exe` + 1 个 `termole.dll`；未新增产品 helper exe，未修改 fzf。测试专用 `pipe-helper.exe` 只在 `BUILD_TESTING=ON` 时构建，不进入发布包。

## 未覆盖/限制

- `pipe-command-output` / OSC 133 不在本次范围；没有把命令结果返回调用方的协议。
- 测试只验证“文本已送达、命令已运行”。命令内部的 fzf/fuzzel UI 没有像素验收；pipe 命令没有控制台，`termole -e` 也不继承 pipe 文本，所以交互选择器必须由命令自己读取 stdin、保存临时文件并打开读取它的窗口，该窗口的可见性不在本票测试范围。
- 命令退出码被完全忽略，没有每命令状态提示；命令内部失败（找不到工具、语法错误）也不诊断，因为 Termole 不解析命令语言。这是票面边界。
- shell 启动失败的诊断由 runner 单元测试覆盖；真实 GUI 中“无 stderr 时弹消息框”的分支没有做故障注入。
- pipe 绑定按 foot 的 `key-bindings` 视为全局设置，profile 不覆盖；嵌套写法会被拒绝。
- 命令进程使用显式句柄列表，只继承 stdin 管道读端和 `NUL`；该限制由正常的 runner/e2e 创建路径覆盖，未单独构造“多余可继承句柄”做反向断言。
