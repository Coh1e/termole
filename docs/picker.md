# Picker（T15/T16 基础实现）

`termole-picker.exe` 是独立的选择器入口：调用方把候选从 stdin 送入，fzf 在**新的控制台窗口**中交互，结果原样从 stdout 返回，诊断写 stderr。它不修改 fzf，也不从终端画面猜结果。

## 用法

```text
termole-picker [--fzf PATH] [fzf arguments...]
```

- stdin：候选原始字节；不追加换行。
- stdout：fzf 原始结果字节；不追加换行、不转码、不排序。
- stderr：picker 诊断（UTF-8）。
- 未找到 fzf：退出码 127。
- fzf 的退出码原样返回；Escape/用户关闭通常为 130。

示例：

```powershell
# 单选
Get-Content names.txt | .\termole-picker.exe --filter banana

# NUL 安全、多选、Unicode
$candidates = "alpha`0banana`0中文`0"
$candidates | .\termole-picker.exe --read0 --print0 --multi
```

`--filter` 用于非交互回归；正常调用不带 `--filter`，由 fzf 打开新控制台窗口。

## 分离

- 候选使用临时文件作为 fzf stdin。
- 结果使用临时文件作为 fzf stdout；picker 在 fzf 退出后读取并写入调用方 stdout。
- fzf 的交互显示使用新控制台，因此不会污染结果流。
- 临时文件在正常、失败和取消路径删除。

## 验证

`t15_picker`：

- ASCII 单选 `banana` 只返回匹配项。
- NUL 输入/输出与中文候选保持原始字节（`中文\0`）。
- 缺失 fzf 返回 127 并写 stderr。
- 完整套件 18/18。

## 限制

- 当前使用独立控制台窗口，而不是嵌入 Termole 的 View/ConPTY 弹窗；T15 要求的“Termole 窗口内选择器”尚未完成。
- 多选、超过管道容量的大输入、调用方提前关闭 stdout、取消清理已有代码路径，但尚未全部加入自动测试。
- fzf 必须由用户安装；Termole 不捆绑 fzf/fd/WSL。
- 运行包当前合并在开发 ZIP 中，T23 分离归档与 T24 无开发工具验收未完成。
