# T18/T19 编辑器与 lazygit 验证

## 已执行（本机，2026-10-30）

工具通过 Scoop 安装，仅用于测试，不进入产品包：

- Neovim 0.12.6
- Helix 25.07.1（`hx.exe`）
- lazygit 0.66.2

`t18_nvim`、`t18_helix`：公开 Session 以 `100×30` ConPTY 启动编辑器，打开临时文件，进入插入模式输入 `HELLO-T18`，Esc 后 `:wq`，等待退出并核对磁盘文件内容。两者通过。

`t19_lazygit`：在独立临时 Git 仓库（`user.name/email` 仅该仓库）启动 lazygit 120×35，等待状态/文件/差异面板绘制，Esc 关闭欢迎弹窗，`q` 退出；通过。测试不修改用户实际仓库。

## 仍未覆盖

- 中文 IME、组合/Emoji、鼠标与 bracketed paste 的真实编辑器流程。
- 独立窗口入口（`termole.exe`）与嵌入宿主的双入口对比；当前测试走公开 Session/ConPTY 嵌入路径。
- Neovim/Helix 的 alternate screen 恢复、光标形状、选择/粘贴、窗口 resize。
- lazygit 的面板导航、diff、中文路径、鼠标/resize；只验证绘制与退出。
- 固定版本/校验和未写入发布基线；工具是全局 Scoop 安装，不满足“项目本地工具”约束。

因此 T18/T19 只能记为**基础兼容路径通过**，不能标记完整验收。
