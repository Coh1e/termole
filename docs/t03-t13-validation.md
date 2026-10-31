# T03–T13 中间切片验证（不含 T09 实机验收）

本文件记录 T03、T04、T05、T06、T07、T08、T10、T11、T12、T13 的实现与验证。T09（中文 IME）已接入组合窗口定位、候选位置和结果提交，但真实 IME 人工验收仍待完成；T14 以后的配置/picker 不在本切片内。

## 已实现

- **T03 退出**：`exit_thread_` 等待目标退出；reader 排空尾部输出后设置 `running=false`；新增 `termole_session_get_exit_code`。CLI 默认成功关窗、非零保留画面并显示退出码状态栏；`--keep-window` / `--close-on-exit` 显式覆盖。保留窗口的进程最终返回目标退出码。
- **T04 生命周期**：1000 行有界回滚；`termole_session_get_scrollback_text`；View 销毁后 Session 继续消费，重新创建 View 可恢复当前屏幕/历史。
- **T05 事件/查询**：标题、响铃、退出、错误事件队列（上限 256，退出/错误优先保留）；`termole_session_poll_event`、退出码、滚动历史查询；标题和响铃由终端解析器产生。
- **T06 兼容**：备用屏幕、DECSTBM 滚屏区、原点/应用光标模式；真实 fzf 0.74.5 绘制候选、接受输入并退出。
- **T07/T08 Unicode**：CJK 双宽、组合字符、Emoji、ZWJ 序列；快照/选择/绘制共用单元格；终端内部单元测试和 ConPTY 输出测试。
- **T10 外观**：`termole_appearance`/`termole_view_options` 支持字体族、字号、系统/深色/浅色主题；`termole_view_set_appearance` 不重建 View；DPI 变化重建格式并重排网格。
- **T11 选择/剪贴板**：View 滚动、视口文本、绝对行选择、选区文本、复制到系统剪贴板、从剪贴板粘贴；Ctrl+C/Ctrl+V；宿主 C ABI 全覆盖。
- **T12 鼠标/粘贴**：终端模式 `?1000/1002/1003/1006/2004/?1`；View 在鼠标报告模式下发送 SGR/X10 事件，Shift 保留本地选择；bracketed paste 边界；应用光标键编码。
- **T13 Applet 窗口**：`--columns/--rows`、`--x/--y`、`--center`、`--floating`、`--borderless`、`--opacity`、`--title`、`--icon`；网格尺寸直接进入 Session 初始尺寸，窗口属性只由独立 Window 外壳应用。

## 测试

`build.cmd`：CTest **16/16** 通过。新增/扩展：

| 测试 | 覆盖 |
| --- | --- |
| `t03_t05_session` | 退出码 7、尾部标记、标题/响铃/退出事件、事件洪泛上限、1000 行历史、View 重附加、备用屏幕、Unicode、外观切换、选区/剪贴板、真实 cmd 粘贴、DPI/字体资源 |
| `t08_unicode_internal` | 直接 Terminal 单元：CJK/组合/Emoji/ZWJ、鼠标/粘贴/应用光标模式解析 |
| `t06_fzf` | 真实 fzf 候选绘制、输入、退出 |
| `t13_cli_applet` | 40×12 网格、居中/透明度/无边框/标题；非法网格/透明度/图标/位置冲突 |
| `t03_cli_exit` | `--close-on-exit` 返回 7；默认失败保留窗口，关闭后返回 7 |
| `t02_cli_*` | 既有 argv/cwd/env/CMD/PS/并行/无控制台回归 |

连续运行 5 次仍待最终复跑；最近一次全量为 15/15。

## 证据

- 主题/字号 18 DIP 的真实窗口：[t10-appearance.png](evidence/t10-appearance.png)
- 独立入口默认窗口：[t02-standalone.png](evidence/t02-standalone.png)
- fzf 测试使用真实 `fzf.exe`（0.74.5），不修改 fzf，不把 fzf 捆绑进产品。
- 当前 Release 体积：`termole.dll` 326,656 B、`termole.exe` 331,776 B；仍为静态 CRT，无 VC++ 运行库 DLL，发布入口不含 smoke/`std::filesystem`。

## 已知限制

- T09 IME 的组合窗口/候选位置与 `GCS_RESULTSTR` 提交已实现，但真实中文 IME 的候选定位、取消、焦点切换仍需人工/自动混合验收，不能以 Unicode 文本 API 代替。
- T12 的鼠标编码由单元/本地选择覆盖；真实点击/拖动/滚轮、bracketed paste、Shift+Insert
  和关闭鼠标模式后的回退已在 [#13](https://github.com/Coh1e/termole/issues/13) 补齐，
  见 [t12-validation.md](t12-validation.md)。
- 1000 行历史不做宽度 reflow；改变列宽后历史行保持旧网格。
- ConPTY 对完整 UAX #29/彩色 Emoji 的字形呈现受系统字体与 ConPTY 限制；本切片验证网格单元、常用 ZWJ 组合和文本往返，不承诺所有旗帜/肤色/高级序列。
- T03 非零退出码只在保留窗口关闭时返回；picker/脚本结果语义仍属 T15。
