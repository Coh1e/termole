# CLI

`termole.exe` 是控制台 launcher；同一个 exe 以内部 GUI 角色承载窗口、Session 和 View。普通模式启动窗口后立即返回；`-e` / `--stdio` 默认等待程序结束并返回退出码。Termole 只负责终端与数据通道，不实现 Shell 管道、选择器或结果协议。

launcher 自身是控制台程序，所以 CMD/PowerShell 会像等待其他控制台程序一样等待它。内部 GUI 角色不是公开入口，`--gui` 只由 launcher 注入，帮助中不列出。

## 用法

```text
termole [options] [--] [program [arguments...]]
termole [options] -e <program> [arguments...]
termole [options] --stdio <program> [arguments...]
```

- 普通模式：程序在 ConPTY 中运行，fd 0/1/2 都接终端；launcher 启动 GUI 后返回 `0`。
- `-e`：foot 语义的 execute，程序仍在 ConPTY 中运行；不转发调用方 stdin/stdout；launcher 等待程序结束并返回退出码。
- `--stdio`：Termole 显式扩展；调用方 stdin/stdout 接到程序，程序 stderr/UI 走 ConPTY；Termole 自身诊断走调用方 stderr；launcher 等待并返回退出码。
- 没有 `program` 时启动默认 Shell：优先 PowerShell 7，否则 Windows PowerShell。
- `--` 之后的所有 token 都作为目标程序及参数，不再解释 Termole 选项。`-e` / `--stdio` 自身就是程序分隔符，后面直接写程序；第一个分隔符之后的 token 全部交给程序，所以不能在其后再写另一个模式开关。
- 目标参数保留空格、引号、空参数和 Unicode；空参数会作为一个空 argv 元素传递。只有需要转义的参数才加引号，避免改变 `cmd /c ...` 这类自行解析命令行的程序的行为。

## 选项

| 选项 | 含义 |
| --- | --- |
| `-e`, `--execute` | foot 语义 execute：PTY 运行程序，不转发调用方 stdin/stdout，等待并返回退出码 |
| `--stdio` | 显式数据通道：调用方 stdin/stdout 接程序，UI/stderr 走 ConPTY，等待并返回退出码 |
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
# 默认 PowerShell；launcher 启动窗口后立即返回
termole.exe

# 普通模式运行程序
termole.exe -- pwsh -NoProfile
termole.exe --cwd C:\repo --env TERM=1 -- git status

# foot 语义 execute：等待程序结束并返回退出码
termole.exe -e lazygit

# 显式数据通道：候选 stdin，结果 stdout，等待并返回退出码
Get-Content names.txt | termole.exe --stdio fzf --multi

# 结果进入下一条命令；控制台 launcher 使管道按顺序执行
Get-Content names.txt | termole.exe --stdio fzf | ForEach-Object { "picked: $_" }
```

## 控制台与窗口

- 从 Explorer 或 `CREATE_NEW_CONSOLE` 启动时，launcher 独占一个新控制台：解析并验证参数后立即隐藏它，并在启动 GUI 后释放，不留下空白控制台窗口。
- 从 CMD/PowerShell 启动时控制台是调用方的：launcher 不隐藏、不释放它。
- GUI 角色始终以 `CREATE_NO_WINDOW` 启动，不与任何控制台绑定，因此调用方关闭控制台或按 Ctrl+C 不会结束终端窗口。需要诊断时它临时 `AttachConsole` 到 launcher 的控制台，写完即分离；`-e`/`--stdio` 的调用方重定向句柄则直接传给 GUI。
- launcher 只隐藏自己新分配的控制台，不隐藏调用方控制台。

## 等待与退出码

- 普通模式：launcher 成功启动 GUI 后返回 `0`，不等待窗口关闭，也不回传 GUI 退出码；需要等待和退出状态时使用 `-e` / `--stdio`。
- `-e` / `--stdio`：launcher 等待 GUI/程序结束。程序正常或失败退出时转交程序退出码；用户在程序退出前关闭窗口返回 `130`；程序启动失败返回 `1`；用法错误返回 `2`；Termole 基础设施失败返回 `125`。
- 不定义 picker 专用错误码，也不承诺 `130` 与任意程序自身退出码不冲突。
- `--stdio` 要求调用方 stdin/stdout 是管道或文件重定向；任一是控制台时返回用法错误 `2`，避免把键盘输入当成候选。`-e` 不要求重定向。

普通模式的 GUI 退出码不会反向传给已经返回的 shell；需要退出码时使用 `-e` 或 `--stdio`。

## 数据通道语义

- `-e`：子进程 fd 0/1/2 都接 ConPTY；调用方管道不会被偷偷转发给子进程。适合 foot 风格的“终端里运行一个交互程序”。
- `--stdio`：只把调用方 stdin/stdout 接到子进程；子进程 stderr 合并到 ConPTY 输出流，终端 UI 仍在 Termole 窗口中。适合 `candidates | termole --stdio fzf | consumer` 这类选择器组合。
- `--stdio` 不读候选、不解析结果、不追加换行、不转码、不排序；字节、背压和退出状态直接传递。Termole 不缓冲数据通道。
- fzf 只是外部程序示例，不是 Termole 的依赖；未安装 fzf 时按普通程序启动失败处理。
- 视觉结论必须用截图/像素证据；`screen_text`/`viewport_text`/`paint_count` 不能替代用户可见性验收。fzf 窗口空白问题已在 #26 修复（渲染目标 DPI 与设备像素坐标混用），像素护栏见 #27。

## Pipe 动作

T25 提供 `pipe-visible` / `pipe-selected` / `pipe-scrollback`：把当前可见文本、选区或回滚以 UTF-8 送到配置的外部命令 stdin。命令 stdout/stderr 丢弃；命令内部可以运行 fzf/fuzzel 等选择器并执行动作。

- 首版不注册默认热键；需要用户在配置中显式绑定按键和命令。
- 命令由 Windows shell 执行，Termole 不实现自己的命令语言。
- 命令自己的临时文件和业务清理由命令负责；若命令通过 `termole -e` 打开新的交互窗口，launcher 的默认等待使命令可以在窗口关闭后再清理。
- `pipe-command-output` 依赖 shell 的 OSC 133 命令边界，不在首版范围。

## 环境变量语义

指定任一 `--env` 时，Termole 读取调用方环境，叠加 `KEY=VALUE` 覆盖，构造新的 Unicode 环境块传给子进程；调用方进程环境不变。每个条目必须包含 `=` 且键非空；值可以包含 `=`、空格和 Unicode。未指定 `--env` 时子进程继承调用方环境。
