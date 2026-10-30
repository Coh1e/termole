# T17 CMD/WSL 基础验证

## 已覆盖

- CMD：`t17_cmd_target` 通过公开 Session 启动交互式 `cmd.exe /D /Q`，发送 `echo CMD-OK`、Enter、`exit`，核对屏幕与退出码 0；`t02_cli_cmd` 另覆盖 CMD 调用方等待/argv/cwd/env。
- WSL：`t17_wsl` 从公开 Session 启动已安装的 WSL 发行版，执行 `sh -c "printf 'WSL-OK\n'"`，在 ConPTY 屏幕读取输出并正常结束。
- 独立入口与嵌入 Session 复用同一 ConPTY 路径；视图/尺寸/鼠标基础已在 T03–T13 覆盖。

## 未完成

- WSL 的中文 IME、粘贴、真实尺寸调整和发行版启动失败诊断只做了基础路径。
- 固定 WSL 发行版/版本与命令历史尚未记录到发布基线。
- Neovim/Helix/lazygit 未安装；T18/T19 未执行。
- 未把 WSL fzf picker 纳入本任务；`cmd /C "带空格和 & 的整段命令"` 的引号边界仍有兼容缺口（交互式 CMD 路径已通过）。

`t17_wsl` 是条件测试：找不到 `wsl.exe` 时不注册；本机通过，不能替代 T24 的 Windows 10/11 发布验收。
