# 命令行用法

Windows 启动器通过同一可执行文件的内部 GUI 角色承载窗口、会话和视图。`--gui` 由启动器传入。启动与等待的设计见 [ADR 0007](adr/0007-console-launcher-wait.md)，数据通道的设计见 [ADR 0006](adr/0006-terminal-selector-boundary.md)。

## 用法

```text
termole [options] [--] [program [arguments...]]
termole [options] -e <program> [arguments...]
termole [options] --stdio <program> [arguments...]
```

- 普通模式下，程序的 stdin、stdout 和 stderr 均连接终端。Windows 启动器成功启动窗口后返回 `0`。
- `-e` 和 `--stdio` 都等待程序结束并返回退出状态。两种模式的输入输出见[数据通道语义](#数据通道语义)。
- 没有 `program` 时启动默认 Shell：优先 PowerShell 7，否则 Windows PowerShell。
- `--` 之后的所有 token 都作为目标程序及参数，不再解释 Termole 选项。`-e` / `--stdio` 自身就是程序分隔符，后面直接写程序；第一个分隔符之后的 token 全部交给程序，所以不能在其后再写另一个模式开关。
- 目标参数保留空格、引号、空参数和 Unicode；空参数会作为一个空 argv 元素传递。只有需要转义的参数才加引号，避免改变 `cmd /c ...` 这类自行解析命令行的程序的行为。

## 选项

| 选项 | 含义 |
| --- | --- |
| `-e`, `--execute` | 在终端内与程序交互，等待并取得退出状态 |
| `--stdio` | 通过管道或文件传入和接收字节，在终端内交互，等待并取得退出状态 |
| `--cwd <directory>` | 子进程工作目录；不存在或不是目录时启动失败 |
| `--env <KEY=VALUE>` | 在继承的调用方环境上覆盖/新增变量，可重复；不修改调用方环境 |
| `--font <FAMILY>` | 字体族 |
| `--font-size <DIP>` | 8..96 DIP |
| `--theme <system\|dark\|light>` | 主题 |
| `--columns/--rows <N>` | Applet 字符网格 1..1000 |
| `--x/--y <PX>` | Applet 位置；不能与 `--center` 同用 |
| `--center` | 居中 |
| `--floating` | 置顶 |
| `--borderless` | 无边框 |
| `--opacity <10..100>` | 透明度百分比 |
| `--title <TEXT>` | 窗口标题 |
| `--icon <PATH>` | 图标文件 |
| `--config <PATH>` | 指定 TOML 配置 |
| `--profile <NAME>` | 应用 `[profiles.NAME]` |
| `--no-config` | 忽略用户配置 |
| `--keep-window` | 目标退出后保留窗口（普通模式） |
| `--close-on-exit` | 目标失败也关闭窗口（普通模式） |
| `--help`, `-h` | 打印用法并退出 0 |
| `--version` | 打印版本并退出 0 |

## 示例

```powershell
# 默认 PowerShell；启动器 启动窗口后立即返回
termole.exe

# 普通模式运行程序
termole.exe -- pwsh -NoProfile
termole.exe --cwd C:\repo --env TERM=1 -- git status

# 在终端中运行程序，等待程序结束并返回退出码
termole.exe -e lazygit

# 显式数据通道：候选 stdin，结果 stdout，等待并返回退出码
Get-Content names.txt | termole.exe --stdio fzf --multi

# 结果进入下一条命令；控制台 启动器 使管道按顺序执行
Get-Content names.txt | termole.exe --stdio fzf | ForEach-Object { "picked: $_" }
```

## 控制台与窗口

Windows 通过 ConPTY 承载终端交互。

- 从 Explorer 或 `CREATE_NEW_CONSOLE` 启动时，启动器 独占一个新控制台：解析并验证参数后立即隐藏它，并在启动 GUI 后释放，不留下空白控制台窗口。
- 从 CMD/PowerShell 启动时控制台是调用方的：启动器 不隐藏、不释放它。
- GUI 角色始终以 `CREATE_NO_WINDOW` 启动，不与任何控制台绑定，因此调用方关闭控制台或按 Ctrl+C 不会结束终端窗口。需要诊断时它临时 `AttachConsole` 到 启动器 的控制台，写完即分离；`-e`/`--stdio` 的调用方重定向句柄则直接传给 GUI。

## 等待与退出码

- 普通模式：启动器 成功启动 GUI 后返回 `0`，不等待窗口关闭，也不回传 GUI 退出码；需要等待和退出状态时使用 `-e` / `--stdio`。
- `-e` / `--stdio`：启动器 等待 GUI/程序结束。程序正常或失败退出时转交程序退出码；用户在程序退出前关闭窗口返回 `130`；程序启动失败返回 `1`；用法错误返回 `2`；Termole 基础设施失败返回 `125`。
- 不定义 picker 专用错误码，也不承诺 `130` 与任意程序自身退出码不冲突。
- `--stdio` 要求调用方 stdin/stdout 是管道或文件重定向；任一是控制台时返回用法错误 `2`，避免把键盘输入当成候选。`-e` 不要求重定向。


## 数据通道语义

- `-e` 将程序的 stdin、stdout 和 stderr 全部连接到终端。调用方等待并取得退出状态。
- `--stdio` 下，调用方通过管道或文件向程序的 stdin 写入字节，从程序的 stdout 原样接收结果字节。程序的交互界面和 stderr 走终端，Termole 自身的诊断写入调用方的 stderr。调用方等待并取得退出状态。
- `--stdio` 不读候选、不解析结果、不追加换行、不转码、不排序；字节、背压和退出状态直接传递。Termole 不缓冲数据通道。
- fzf 只是外部程序示例，不是 Termole 的依赖；未安装 fzf 时按普通程序启动失败处理。

## Pipe 动作

可以使用 `pipe-visible` / `pipe-selected` / `pipe-scrollback`：把当前可见文本、选区或回滚以 UTF-8 送到配置的外部命令 stdin。命令 stdout/stderr 丢弃；命令内部可以运行 fzf/fuzzel 等选择器并执行动作，但选择器的终端/UI 由命令自己提供。

- 首版不注册默认热键；需要用户在配置中显式绑定按键和命令，配置形状见 [config.md](config.md#pipe-动作按键)。
- 命令由 `%COMSPEC%`（缺省 `cmd.exe`）以 `/d /s /c "<command>"` 执行，Termole 不实现自己的命令语言。
- 文本固定 UTF-8，行分隔符是 `\n`，不追加尾随换行：`pipe-visible` 使用当前 View 视口（去掉每行行尾空格和末尾空行），`pipe-selected` 使用当前选区（保留空格），`pipe-scrollback` 使用 Session 保留的、有界的最多 1000 行回滚。
- 命令 stdout/stderr 指向 `NUL`，不进入终端，也不污染调用方 stdout；命令退出码不影响 Termole 会话，只有 shell 进程无法启动写诊断，命令内部失败（含工具缺失/语法错误）由命令退出码表示且不被解析。
- 命令自己负责 UI：pipe 命令没有控制台，`termole -e` 打开的新窗口不继承 pipe 文本；需要选择器 UI 的命令应自己保存 stdin 并打开读取它的窗口。
- 命令自己的临时文件和业务清理由命令负责；若命令通过 `termole -e` 打开新的交互窗口，启动器 的默认等待使命令可以在窗口关闭后再清理。
- `pipe-command-output` 依赖 shell 的 OSC 133 命令边界，不在首版范围。

## 环境变量语义

指定任一 `--env` 时，Termole 读取调用方环境，叠加 `KEY=VALUE` 覆盖，构造新的 Unicode 环境块传给子进程；调用方进程环境不变。每个条目必须包含 `=` 且键非空；值可以包含 `=`、空格和 Unicode。未指定 `--env` 时子进程继承调用方环境。

## 本地会话

在一个终端启动服务，再从另一个终端连接。两端使用同一个 Termole 可执行文件。

```sh
termole --server --pipe dev
termole --client --pipe dev --detach
```

在服务端命令末尾加 `-- PROGRAM [ARGS...]` 可指定子程序。`--pipe` 指定端点名，默认值为 `default`。名称由 1 至 32 个 ASCII 字母、数字、连字符或下划线组成。每个端点托管一个会话，同时只接受一个视图。多个会话分别使用不同端点。

关闭视图时，默认同时结束服务端和子程序。示例中的 `--detach` 让服务端保留子程序和终端状态。再次连接同一端点即可恢复。视图连接期间若子程序退出，客户端返回其退出码。

客户端显示服务端的可见网格。拖动鼠标可选择可见单元格，按 Ctrl+Shift+C 复制，按 Ctrl+Shift+V 粘贴。

Windows 使用命名管道，仅允许当前用户在当前登录会话中访问。Linux 和 macOS 使用 Unix 域套接字，所在目录归当前用户所有，权限为 `0700`。在 Unix 上强制终止服务端可能留下套接字路径，换一个端点名即可立即启动新会话。

本地协议采用带版本号和长度前缀的字节格式，传输输入字节、视口尺寸、终端模式、可见单元格和退出状态。当前版本为 1。首次连接的版本不匹配时，服务端返回不兼容错误并拒绝连接。后续请求版本不匹配时，服务端结束通信。客户端收到版本异常的回复时按连接关闭处理。
