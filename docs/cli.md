# CLI

T02 起，独立入口支持从 CMD、PowerShell 或任何进程启动指定目标程序。Termole 只负责启动目标，不实现 Shell 管道或命令语言；管道仍由调用方 Shell 组合。

## 用法

```text
termole [options] [--] [program [arguments...]]
```

- 没有 `program` 时启动默认 Shell：优先 PowerShell 7，否则 Windows PowerShell。
- `--` 之后的所有 token 都作为目标程序及参数，不再解释 Termole 选项。只写 `--` 而不提供目标时仍启动默认 Shell。
- 没有 `--` 时，首个非选项 token 是目标程序，之后的 token 全部原样传给目标；因此目标参数以 `-` 开头也不需要额外转义。
- 目标参数保留空格、引号、空参数和 Unicode；空参数会作为一个空 argv 元素传递。

选项：

| 选项 | 含义 |
| --- | --- |
| `--cwd <directory>` | 子进程工作目录；不存在或不是目录时启动失败 |
| `--env <KEY=VALUE>` | 在继承的调用方环境上覆盖/新增变量，可重复；不修改调用方环境 |
| `--help`, `-h` | 打印用法并退出 0 |
| `--version` | 打印版本并退出 0 |

## 示例

```powershell
# 默认 PowerShell
termole.exe

# 指定目标程序
termole.exe -- pwsh -NoProfile

# 工作目录与环境
termole.exe --cwd C:\repo --env TERM=1 -- git status

# 目标参数以 - 开头
termole.exe -- git --version

# 调用方组合管道
Get-Content names.txt | termole.exe -- some-tui
```

## 诊断与退出码

- 用法错误（未知选项、缺少选项值）：stderr 诊断，退出码 `2`。
- 目标程序/工作目录/环境变量启动失败：stderr 诊断，退出码 `1`；无控制台时以 MessageBox 呈现。
- `--help`/`--version`：stdout，退出码 `0`。
- 目标程序的非零退出码尚未由 T02 转交；退出码传播与最后输出保留属于 T03。

`termole.exe` 是 Windows GUI 子系统程序。正常启动是异步的；需要等待时使用：

```cmd
start "" /wait termole.exe -- pwsh -NoProfile
```

```powershell
Start-Process -FilePath .\termole.exe -ArgumentList '--','pwsh','-NoProfile' -Wait
```

目标进程退出后 Termole 会先让 reader 排空 ConPTY 尾部输出，再关闭窗口，等待调用因此返回；T03 再补非零退出码转交和保留窗口策略。在 CMD/PowerShell 的 `start /wait` / `Start-Process -Wait` 调用下，T02 已验证等待、启动错误退出码和目标进程获得真实 ConPTY 标准输出。

## 环境变量语义

指定任一 `--env` 时，Termole 读取调用方环境，叠加 `KEY=VALUE` 覆盖，构造新的 Unicode 环境块传给子进程；调用方进程环境不变。每个条目必须包含 `=` 且键非空；值可以包含 `=`、空格和 Unicode。未指定 `--env` 时子进程继承调用方环境。
