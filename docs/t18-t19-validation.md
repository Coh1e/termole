# T18/T19 编辑器与 lazygit 验证

## 状态：未执行

本机没有安装 Neovim、Helix 或 lazygit，且项目约束要求工具/缓存保留在项目内、不修改全局环境；本轮未下载第三方二进制。因此 T18/T19 的验收项目前**未完成**，不能标记为通过。

## 需要执行的最小流程

- Neovim/Helix：固定版本与最小配置，打开文件、输入中英文、导航、选择/粘贴、resize、保存、退出；覆盖 alternate screen、光标形状、IME、组合/Emoji、鼠标与 bracketed paste。
- lazygit：独立临时 Git 仓库，固定版本；浏览状态、面板导航、查看 diff、中文路径、鼠标/resize、退出恢复。
- 两种编辑器与 lazygit 均需独立窗口和嵌入入口各一次。

已有底层能力：alternate screen、DSR/DA、宽字符/组合、鼠标模式、bracketed paste、滚屏区。实际兼容缺口必须由真实流程发现，不能以“上游宣称支持”替代。

## 后续

在有工具的环境把可执行文件放入 `.local/tools`，使用固定版本/校验和，执行上述流程并记录证据；在此之前 T24 不能完成。
