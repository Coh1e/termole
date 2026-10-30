# T24 首版交付验收

## 状态：未完成

本机只能执行 Windows 11 x64 的一部分自动与手工检查；没有 Windows 10 22H2、真实中文 IME 操作者、干净无开发工具机器或独立发布签名环境。因此不能把 T24 标记为通过。

## 已执行

- Windows 11 x64：`build.cmd` CTest 24/24；`package.cmd` 构建、安装、CPack；外部 `find_package(Termole)` SDK 宿主编译链接。
- 独立/嵌入入口、CMD、Windows PowerShell、WSL `sh`、真实 fzf、picker 字节通道、Applet 网格、选择/剪贴板、配置/Profile、无控制台启动。
- Release 静态 CRT，无 VC++ 运行库 DLL；体积和空闲/启动/吞吐初步数据见 [T21](t21-performance.md)。

## 未执行/未通过

- Windows 10 22H2 全量验收。
- 真实中文 IME：候选定位、提交/取消、焦点切换的人工/自动混合验收；代码已接入 `WM_IME_*`，但没有实际 IME 证据。
- Neovim、Helix、lazygit（未安装，见 [T18/T19](t18-t19-validation.md)）。
- 无开发工具机器运行便携包；独立运行包/开发包拆分归档。
- 嵌入式 picker/多选、大结果背压、长时间 soak。
- 许可/来源的发布级复核、签名和安装器。
- 所有声明支持程序的 Windows 10/11 双系统矩阵。

## 结论

当前是“Windows 11 上的可运行开发切片 + 开发/便携 ZIP 雏形”，不是首版交付验收。未执行项保持未完成，不用本机探针或旧证据替代。
