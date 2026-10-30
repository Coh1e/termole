# Picker

Picker 是 `termole.exe` 的内部模式，不再有独立 `termole-picker.exe`。最终发布只包含：

```text
termole.exe
termole.dll
```

## 用法

```text
termole --picker [--picker-arg ARG]... [其他 Termole 选项]
```

- stdin：候选原始字节；不追加换行。
- stdout：fzf 原始结果字节；不追加换行、不转码、不排序。
- stderr：picker 诊断。
- `--picker-arg` 可重复，用于 `--read0`、`--print0`、`--multi`、`--filter` 等。
- fzf 必须由用户安装；未找到时退出码 127。

示例：

```powershell
Get-Content names.txt | .\termole.exe --picker
Get-Content names.txt | .\termole.exe --picker --picker-arg --multi
$candidates | .\termole.exe --picker --picker-arg --read0 --picker-arg --print0
Get-Content names.txt | .\termole.exe --picker --picker-arg --filter --picker-arg banana
```

## 实现

`termole.exe` 把候选写入临时文件，结果写入临时文件，然后以公开 Session/View 直接启动 fzf：

- fzf 的 stdin 是候选文件；
- fzf 的 stdout 是结果文件；
- fzf 的 stderr 仍是 ConPTY 控制台，UI 由 fzf 打开 `CONOUT$`；
- 目标退出后 Termole 读取结果写回调用方 stdout，并关闭窗口。

因此 picker 和正常终端共用同一个 exe/dll，不需要 helper 进程或额外 exe。

## 验证

`t15_embedded_picker`：Termole 窗口内 ASCII 单选、NUL/Unicode、`--multi`、12k 行输入、stdout 提前关闭、120 KB 慢读背压。完整套件 30/30。

## 限制

- 超大结果和所有取消路径仍未覆盖。
- fzf 不捆绑；缺失时返回 127。
- 真实 GUI 点击/鼠标仍属 T18/T19 的完整验收范围。
