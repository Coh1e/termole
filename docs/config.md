# TOML 配置与 Profile

Termole 的独立入口读取一个小型 TOML 子集；宿主嵌入不读取用户配置，也不会因配置错误弹窗或覆盖宿主布局。

## 文件位置与优先级

1. 默认值。
2. `%APPDATA%\Termole\config.toml`（不存在时跳过）。
3. 选中的 `[profiles.<name>]`。
4. CLI 参数。

`--config <PATH>` 使用指定文件；`--no-config` 跳过所有用户配置；`--profile <NAME>` 选择配置组。`--help`、`--version` 和 CLI 语法错误在读取配置前处理。

配置读取失败时，诊断包含文件路径和行号，Termole 继续使用默认值；CLI 参数错误仍然直接失败（退出码 2）。

## 支持的键

| 键 | 类型 | 说明 |
| --- | --- | --- |
| `font` | string | 字体族；缺省使用 Consolas 并回退 Courier New |
| `font_size` | integer 8..96 | DIP 字号 |
| `theme` | `"system"` / `"dark"` / `"light"` | 主题 |
| `columns`, `rows` | integer 1..1000 | Applet 网格 |
| `x`, `y` | integer | Applet 位置 |
| `center` | boolean | 居中 |
| `floating` | boolean | 置顶 |
| `borderless` | boolean | 无边框 |
| `opacity` | integer 10..100 | 透明度百分比 |
| `title` | string | 窗口标题 |
| `icon` | string | 图标文件 |
| `program` | string | 未在 CLI 指定目标时使用的程序 |
| `args` | string array | 目标参数 |
| `cwd` | string | 工作目录 |
| `env` | string array | `KEY=VALUE` 覆盖 |
| `keep_window` | boolean | 目标退出后保留窗口 |
| `close_on_exit` | boolean | 目标失败也关闭窗口 |

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

## 错误示例

```text
Termole: configuration error: C:\Users\me\AppData\Roaming\Termole\config.toml:7: theme must be "system", "dark" or "light"
Using defaults.
```

`--config` 指向不存在文件、未知键、类型错误、未闭合字符串/数组、缺失 profile 都会给出路径与行号。配置不是插件或应用包；Termole 不执行配置中的脚本。
