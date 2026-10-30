# T18/T19 编辑器与 lazygit 验证

## 已执行（本机，2026-10-30）

工具通过 Scoop 安装，仅用于测试，不进入产品包：

- Neovim 0.12.6
- Helix 25.07.1（`hx.exe`）
- lazygit 0.66.2

`t18_nvim`、`t18_helix`：公开 Session 以 `100×30` ConPTY 启动编辑器，打开临时文件，进入插入模式输入 `HELLO-T18`，resize 后 Esc、`:wq`，等待退出并核对磁盘文件内容。两者通过。

`t18_nvim_shell`、`t18_helix_shell`：在交互式 CMD 中启动编辑器，编辑保存后退出，检查主屏幕提示符恢复（alternate screen 返回主屏）并通过。两者通过。

`t19_lazygit`：在独立临时 Git 仓库（`user.name/email` 仅该仓库）启动 lazygit 120×35，等待状态/文件/差异面板绘制，Esc 关闭欢迎弹窗，resize 后 `q` 退出；通过。测试不修改用户实际仓库。

## 仍未覆盖

- Neovim/Helix 的 alternate screen 主屏恢复已由 CMD 内流程覆盖；lazygit 仍只测直接目标，未覆盖 CMD 内恢复。
- 中文 IME、组合/Emoji、鼠标与 bracketed paste 的真实编辑器流程。
- 独立窗口入口（`termole.exe`）与嵌入宿主的双入口对比；当前测试走公开 Session/ConPTY 嵌入路径。
- 编辑器光标形状与选择/粘贴尚未自动断言。
- 固定版本/校验和未写入发布基线；工具是全局 Scoop 安装，不满足“项目本地工具”约束。

因此 T18/T19 只能记为**基础兼容路径通过**，不能标记完整验收。
