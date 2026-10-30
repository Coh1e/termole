# T14 配置与 Profile 验证

## 实现

- `apps/common/config.*`：项目内小型 TOML 子集解析器，无第三方依赖；支持根键与 `[profiles.<name>]`、字符串/整数/布尔/字符串数组、BOM、注释、单双引号字符串。
- 默认路径 `%APPDATA%\Termole\config.toml`；`--config`、`--profile`、`--no-config`。
- 优先级：默认值 → 用户配置根 → Profile → CLI。CLI 显式值不被配置覆盖。
- 配置错误包含文件与行号，独立入口提示后继续使用默认值；CLI 语法错误仍退出 2。宿主不读取用户配置。
- 配置不能执行脚本；不支持 QuickJS/插件。

## 验证

`t14_cli_config`（CTest）：

1. 根配置 `columns=30/rows=8`，Profile `work` 覆盖为 44×9，并设置受控子程序、参数、cwd/env；实际子程序报告 `console.cols=44`、`console.rows=9`。
2. CLI `--columns 50 --rows 10` 覆盖 Profile，报告 50×10。
3. `columns =` 的坏配置输出 `bad.toml:1: missing value`，仍运行 CLI 指定的目标并成功退出。
4. `--no-config` 不报告坏配置。

`t14_cli_config` 通过；完整套件 24/24。

## 已知限制

- 只实现 Termole 需要的 TOML 子集，不是完整 TOML 1.0（无日期、浮点、内联表、多行字符串）。
- Profile 是配置组，不是插件/应用包。
- 默认用户配置路径与配置 UI 尚未做可视化编辑器。
