# Picker（T15/T16 基础实现）

Termole 提供两个 picker 入口：

- `termole.exe --picker`：从 stdin 读取候选，在 **Termole 自己的 View/ConPTY 窗口**中显示 fzf，结果原样写 stdout；这是主入口。
- `termole-picker.exe`：独立调用时在新控制台窗口显示 fzf；嵌入模式（`--embedded`）由 `termole.exe` 使用。

fzf 不被修改，也不从终端画面猜结果。

## 用法

```text
termole --picker [--picker-arg ARG]... [其他 Termole 选项]
termole-picker [--fzf PATH] [fzf arguments...]
```

- stdin：候选原始字节；不追加换行。
- stdout：fzf 原始结果字节；不追加换行、不转码、不排序。
- stderr：picker 诊断（UTF-8）；嵌入模式的 fzf 诊断/UI 在 ConPTY，picker 内部错误通过临时诊断文件转回调用方 stderr。
- 未找到 fzf：退出码 127。
- fzf 的退出码原样返回；Escape/用户关闭通常为 130。
- `--picker-arg` 可重复，用于 `--read0`、`--print0`、`--multi`、`--filter` 等。

示例：

```powershell
# 嵌入 Termole 窗口的单选
Get-Content names.txt | .\termole.exe --picker

# NUL 安全、多选、Unicode
$candidates = "alpha`0banana`0中文`0"
$candidates | .\termole.exe --picker --picker-arg --read0 --picker-arg --print0 --picker-arg --multi

# 非交互回归
Get-Content names.txt | .\termole.exe --picker --picker-arg --filter --picker-arg banana
```

## 分离

- `termole.exe` 把候选写入临时文件，创建结果/诊断临时文件。
- 它以公开 Session/View 启动 `termole-picker.exe --embedded`；fzf 的 stdin/out 是数据文件，stderr 是 ConPTY 控制台，UI 由 fzf 打开 `CONOUT$`。
- 目标退出后 Termole 把结果和诊断分别复制到调用方 stdout/stderr，再关闭窗口。
- 临时文件在成功、失败和取消路径删除。

## 验证

`t15_picker`：独立入口的 ASCII 单选、NUL/Unicode、缺失 fzf 127。
`t15_embedded_picker`：Termole 窗口内的 ASCII 单选、NUL/Unicode、`--multi` 参数传递、12k 行输入、stdout 提前关闭。完整套件 26/26。

## 限制

- 多选/大结果背压、调用方提前关闭 stdout、候选输入提前结束尚未全部自动覆盖。
- 独立 `termole-picker.exe` 仍使用新控制台；主 `termole.exe --picker` 使用 Termole 窗口。
- fzf 必须由用户安装；Termole 不捆绑 fzf/fd/WSL。
- 运行包当前合并在开发 ZIP 中，T23 分离归档与 T24 无开发工具验收未完成。

