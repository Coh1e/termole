# ADR 0010：窗口定义与 View 布局

- 状态：accepted（分阶段实现；`[window]`、width/height、columns/rows、标题、位置、居中、置顶、无边框、透明度已接入）
- 日期：2026-11-19
- 相关：ADR 0007、ADR 0009

## 背景

Termole 需要“窗口定义灵活、现代化”，同时保持一个 Session 对应一个 View 的既有模型。窗口定义要能被 CLI、配置文件和脚本共同影响，且不引入第二套窗口系统。

## 决策

1. 窗口定义是配置文档的一部分，建议语法：

   ```toml
   [window]
   width = 1000
   height = 640
   columns = 80
   rows = 24
   title = "Termole"
   decorations = true
   opacity = 1.0
   position = "center"   # center | x,y
   always_on_top = false
   ```

2. 优先级：**CLI > 脚本/配置 > 默认值**；与 Session 配置同一套覆盖规则。
3. `columns`/`rows` 与像素宽高冲突时，以字符网格为准，窗口按字体度量反推；两者都给出且不匹配则报 `INVALID_ARGUMENT`（沿用现有 `t01_host_api` 行为）。
4. 一个 Session 只能 attach 一个 View；View 销毁不终止 Session，重新 attach 复用同一回滚/屏幕状态。
5. 窗口定义只影响 View 创建，不改变 Session 的 ConPTY 生命周期。
6. 第一版只实现 Win32 顶层窗口；不做多窗口/标签页，直到 server/client（ADR 0011）落地。

## 后果

- 需要配置解析、校验、CLI 覆盖与视觉测试（尺寸/标题/装饰/位置）。
- 脚本接入时窗口定义通过宿主能力表暴露，不直接操作 HWND。
- 保持“轻量启动”：默认不加载脚本、不创建多余窗口。
