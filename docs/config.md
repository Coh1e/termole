# TOML 配置与 Profile

独立入口支持下列 TOML 配置。嵌入宿主通过 [SDK 选项](sdk.md#宿主责任与授权)传入设置。

## 文件位置与优先级

1. 默认值。
2. `%APPDATA%\Termole\config.toml`（不存在时跳过）。
3. 选中的 `[profiles.<name>]`。
4. CLI 参数。

`--config <PATH>` 使用指定文件；`--no-config` 跳过所有用户配置；`--profile <NAME>` 选择配置组。`--help`、`--version` 和 CLI 语法错误在读取配置前处理。

配置读取失败时，诊断包含文件路径和行号，Termole 继续使用默认值；CLI 参数错误仍然直接失败（退出码 2）。

## 本地 QuickJS 插件

Windows 标准包内置 QuickJS。解压后在 PowerShell 中运行：

```powershell
.\bin\termole.exe --config .\share\termole\examples\markdown-quote.toml
```

选中终端文字，打开视图的右键菜单并选择「复制为 Markdown 引用」，再粘贴到编辑器。示例会去掉跨行选区中非末行的末尾空格，再为每行添加 `> `。要停用插件，从配置中移除 `plugins` 项并重启 Termole。

将 `plugins = ["markdown-quote.js"]` 放在配置文件的全局部分。相对路径以配置文件所在目录为基准，也可以使用绝对路径。只启用你信任的本地脚本。每个脚本在加载时用 `termole.registerCommand(label, callback)` 注册一个同步命令。回调可用 `termole.getSelection()` 读取打开菜单的视图选区，用 `termole.writeClipboard(text)` 写入剪贴板。回调成功返回后才提交剪贴板文本。

## 支持的键

| 键 | 类型 | 说明 |
| --- | --- | --- |
| `font` | 字符串 | 字体族；缺省使用 Consolas 并回退 Courier New |
| `font_size` | 整数 8..96 | DIP 字号 |
| `theme` | `"system"` / `"dark"` / `"light"` | 主题 |
| `columns`, `rows` | 整数 1..1000 | Applet 网格 |
| `x`, `y` | 整数 | Applet 位置 |
| `center` | 布尔值 | 居中 |
| `floating` | 布尔值 | 置顶 |
| `borderless` | 布尔值 | 无边框 |
| `opacity` | 整数 10..100 | 透明度百分比 |
| `title` | 字符串 | 窗口标题 |
| `icon` | 字符串 | 图标文件 |
| `program` | 字符串 | 未在 CLI 指定目标时使用的程序 |
| `args` | 字符串数组 | 目标参数 |
| `cwd` | 字符串 | 工作目录 |
| `plugins` | 字符串数组 | Windows 独立终端启用的本地 `.js` 文件，所有 Profile 共用 |
| `env` | 字符串数组 | `KEY=VALUE` 覆盖 |
| `keep_window` | 布尔值 | 目标退出后保留窗口 |
| `close_on_exit` | 布尔值 | 目标失败也关闭窗口 |

Windows 路径建议使用单引号字面量，避免 TOML 基本字符串的反斜杠转义：

```toml
font = "Consolas"
font_size = 18
theme = "system"
columns = 100
rows = 30

[profiles.work]
program = 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
args = ['-NoProfile']
cwd = 'D:\work'
env = ['TERMOLE_PROFILE=work']
```

## 窗口尺寸

Windows 的 `[window]` 表使用上列窗口键。像素尺寸 `width` 接受 200..10000，`height` 接受 120..10000；`decorations = false` 等同于 `borderless = true`。定位用 `center` 或 `x`、`y`，置顶用 `floating`，透明度用整数百分比。显式 CLI 尺寸优先于配置中的同名尺寸。

像素尺寸与字符网格可同时填写。普通 Windows GUI 在同时指定 `columns` 和 `rows` 时按字体度量换算窗口尺寸，此路径不使用配置中的像素宽高。C GUI 则用像素宽高创建窗口，用行列数初始化会话，再按客户区调整网格。两组数值不匹配不会触发独立入口的配置错误。

## Pipe 动作按键

`pipe-visible`、`pipe-selected`、`pipe-scrollback` 各有一个可选的配置表。没有配置表时不注册任何 pipe 热键；默认行为不变。

```toml
[pipe.visible]
key = 'ctrl+shift+f9'
command = 'my-selector --source=visible'

[pipe.selected]
key = 'ctrl+shift+f10'
command = 'my-selector --source=selection'

[pipe.scrollback]
key = 'ctrl+shift+f11'
command = 'my-selector --source=scrollback'
```

每个表必须同时有 `key` 和 `command`，且键名只允许 `visible`、`selected`、`scrollback`。配置表只能出现在根一级；与 foot 的 `key-bindings` 一样，pipe 绑定是全局设置，`[profiles.*]` 不覆盖它们。嵌入宿主按现有约定不读取用户配置，因此不会注册 pipe 热键；它使用公开查询自行组合文本和命令。

fzf/fuzzel 等交互程序需要自己的终端。pipe 命令本身没有控制台，Termole 也不会把 pipe 文本交给 `termole -e` 打开的新窗口；需要 UI 的命令应自己读取 stdin、保存到由它负责的临时文件，再用 `termole -e` 打开读取该文件的程序，并在窗口关闭后清理。

### `key`

按键和弦（key chord）由修饰键和基础键组成，用 `+` 连接，大小写不敏感。

- 修饰键：`ctrl`、`shift`、`alt`，顺序不限；至少要包含 `ctrl` 或 `alt`，避免抢占普通输入。
- 基础键：`a`–`z`、`0`–`9`、`f1`–`f24`、`insert`、`delete`、`home`、`end`、`pageup`、`pagedown`、`up`、`down`、`left`、`right`、`space`、`tab`、`enter`、`escape`、`backspace`。
- 同一按键不能绑定到两个动作；按键冲突、未知修饰键或未知基础键都是配置错误，诊断包含文件路径和行号。
- 内置快捷键 `ctrl+c`、`ctrl+v`、`ctrl+shift+c`、`ctrl+shift+v` 和 `shift+insert` 不能被 pipe 绑定覆盖，配置会报冲突。
- 配置的 pipe 按键优先于终端输入；不要覆盖其他常用终端快捷键。

### `command`

命令字符串原样交给 Windows shell 执行：`%COMSPEC%`（缺省 `cmd.exe`）以 `/d /s /c "<command>"` 运行。Termole 不实现自己的命令语言，不解析引号或管道。

- 命令的 stdin 是 Termole 送出的文本；stdout 和 stderr 指向 `NUL`，不进入终端，也不污染调用方 stdout。
- 命令退出码不影响 Termole 会话。只有 shell 进程无法启动时才写诊断；命令内部的失败（包括找不到工具、命令语法错误）由命令自己的退出码表示，Termole 不解析、不诊断。
- 需要交互式选择 UI 的命令必须自己获得终端。pipe 命令没有控制台，`termole -e` 打开的新窗口也不继承 pipe 文本；命令应自己保存 stdin，再打开读取该文件的窗口，并在窗口关闭后清理（launcher 的默认等待保证清理发生在窗口关闭之后，见 [ADR 0007](adr/0007-console-launcher-wait.md)）。
- 临时文件、选择结果和业务清理由命令自己负责；Termole 只负责自己的管道和进程收尾。

### 送出的文本

文本固定为 UTF-8，不带 BOM，行分隔符是单个 `\n`（LF），**不追加尾随换行**。

| 动作 | 文本来源 | 边界 |
| --- | --- | --- |
| `pipe-visible` | 当前 View 视口（受滚屏位置影响） | 每行去掉行尾空格，去掉末尾空行 |
| `pipe-selected` | 当前 View 选区 | 保留选区矩形内的每个 cell（含空格），不 trim |
| `pipe-scrollback` | Session 保留的全部回滚 | 最多 1000 行；每行去掉行尾空格 |

文本为空时命令仍会运行，并立即收到 EOF。三个动作分别复用 View 视口、View 选区和 Session 回滚的现有公开定义（`termole_view_get_viewport_text`、`termole_view_get_selection_text`、`termole_session_get_scrollback_text`）。

## 错误示例

```text
Termole: configuration error: C:\Users\me\AppData\Roaming\Termole\config.toml:7: theme must be "system", "dark" or "light"
Using defaults.
```

`--config` 指向不存在文件、未知键、类型错误、未闭合字符串/数组、缺失 profile 都会给出路径与行号。配置不是插件或应用包；Termole 不执行配置中的脚本。
