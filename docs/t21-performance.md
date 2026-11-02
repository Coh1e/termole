# T21 性能基线与按需刷新

任务：[#22](https://github.com/Coh1e/termole/issues/22)。日期：2026-11-02。
起点：`157a967`，Windows 11 x64（10.0.26300），AMD64 Family 25 Model 117，16 逻辑处理器；MSVC 14.51 / CMake 4.4 / Ninja，Release、静态 CRT。

本次增加测量和验证，不改产品实现、ABI、入口或等待策略。没有测出足以支持产品优化的证据，因此不作推测性优化。这是本机基线，不是性能承诺或 Windows 10/11 发布验收。

## 测量口径

- **独立入口**：通过公开的普通模式及 `-e` 启动同一个受控子程序。按唯一标题找到窗口，再用 HWND 所属 PID 获取真正的 GUI 进程；普通 launcher 已返回，不把它的短生命周期当作 GUI 性能。记录 launcher/GUI 的原生退出码，任何异常使脚本失败；托管退出码另存作诊断。
- **启动**：从调用 Start-Process 到发现 GUI 窗口、再到观察到目标 PID 文件；包括测量脚本开销和 5 ms 轮询。窗口存在不是画面可见证据，首次可见画面另由像素测试测量。
- **冷/热条件**：每次都是新 launcher/GUI/子进程；每组第 1 次与后续 4 次分开记录。没有清空 OS 文件/驱动缓存，也没有重启；这是进程冷启动和重复启动，不代表严格的机器冷缓存启动。旧报告约 1 秒的值来自旧入口和不同负载，不作前后优化比较。
- **空闲**：受控子程序等待 8 秒；目标就绪后预热 1 秒，采样 GUI 约 5 秒的内核+用户 CPU 时间。CPU 毫秒不是 CPU 百分比，也不含子进程、ConHost 或 GPU。嵌入入口的空闲用例持续处理窗口消息，避免暂停消息循环掩盖重绘。
- **内存**：真正 GUI/嵌入宿主的 working set，及 OS PeakWorkingSet 高水位；不以二进制体积代替运行内存，不把 launcher、GUI、ConHost、目标程序合成一个数。独立脚本每 10 ms 采样一次；输出负载前后各保留 1 秒以观察输出期高水位，最后一次采样之后的退出期峰值可能遗漏。
- **吞吐**：同一 helper 输出 50,000 行 `line-%04d\r\n`，每行独立 WriteFile；前后各等待 1 秒。记录启动到 GUI 退出总耗时；扣除固定 2 秒后仍包括启动、VT/网格更新、绘制及清理，是端到端吞吐代理，不是纯解析器速度。
- **帧延迟**：通过真实 View 的 WM_CHAR 输入，子程序无 echo/行缓冲地读取一个字节，交替绘制红/绿整屏。2 ms 请求轮询三个固定 client 像素，观察预期颜色后结束计时；包括输入排队、子程序、ConPTY、解析、绘制、GDI 可观察表面和轮询开销。20 个样本的中位数取排序后第 10、11 项的平均，P95 采用 nearest-rank 第 19 项。它不是单次 renderer 调用时间、GPU present 时间或屏幕光子延迟。计时期间不调用 PrintWindow、UpdateWindow 或强制重绘；截图只在计时之后生成。两个新视觉用例 RUN_SERIAL，避免与其他 CTest 窗口互相遮挡。

## 独立入口结果

在一轮 42/42 全量通过后，用相同正常桌面权限顺序完成 15 次测量：[measurements](evidence/t21-measurements.json)；环境和产品 SHA-256：[environment](evidence/t21-environment.json)。其后只修正了测试的中位数汇总公式，并重新运行全量；产品和负载 helper 未变，因此不重复采集资源基线。工作目录与缓存均在项目内，测量时未修改用户配置，也未与视觉测试并行。

| 场景 | 结果 |
| --- | --- |
| 普通模式第 1 次发现窗口 | 282.49 ms |
| 普通模式随后 4 次发现窗口 | 231.16–243.21 ms（精确样本见 JSON） |
| `-e` 5 次发现窗口 | 213.37–231.55 ms |
| 普通模式 GUI 空闲 CPU / 约 5 s | 0–15.625 ms |
| `-e` GUI 空闲 CPU / 约 5 s | 0–15.625 ms |
| 普通模式 GUI 空闲 working set | 49,926,144–51,912,704 B |
| `-e` GUI 空闲 working set | 49,901,568–54,161,408 B |
| 50,000 行：含 2 s 固定等待的总耗时，5 次 | 3,224.13–3,445.15 ms |
| 同负载：扣除固定等待，仍含启动和清理 | 1,224.13–1,445.15 ms，约 34.6k–40.8k 行/s |
| 同负载：观察到的 GUI peak working set | 56,320,000–60,567,552 B（约 53.7–57.8 MiB） |

普通模式每次在目标结束前 launcher 已返回；`-e` 在目标存活时 launcher 未返回。最终 15 次 launcher/GUI 原生退出码均为 0；这只是测量正确性的前提，等待契约仍归 `t26_*` 测试，不在本票另建一套契约测试。

## 嵌入、空闲和可见帧

`t21_idle_paint` 记录创建 Session 前、无 View 的 Session、首次 View 和空闲阶段 working set / peak working set。持续处理消息的 5 秒采样要求 paint_count 不增长，随后输出必须触发 paint；计数只证明按需调度，可见性由下述像素测试负责。采样窗口保持未激活，避免前台恢复时合法的焦点重绘混入空闲统计；不是对焦点切换时“永不绘制”的承诺。

最终全量运行：创建 Session 前 6,897,664 B，无 View 时 9,109,504 B；View 就绪 47,898,624 B，空闲 47,972,352 B，观察到的 peak 48,566,272 B；5,003.85 ms 空闲中 CPU 增量为 0 ms，paint_count 保持不变。另有单次和连续五次空闲检查通过。

未使用图形能力的初始化以结构检查补充：`Session::create` 不创建 Renderer；`View::initialize` 才调用 `Renderer::create`，后者经 `ensure_graphics` 延迟创建 D2D/DWrite factory（`src/view/view.cpp`、`src/view/renderer.cpp`）。这不等于“系统没有加载图形 DLL”，也没有用私有计数器替代公开边界。

`visual_t21_latency` 和 `visual_t21_latency-standalone` 各采集 20 次交替颜色；初始蓝色和每一次红/绿都必须在真实 View 像素中出现。5 秒仅是失败看门狗，不是性能 SLA。[逐帧与空闲输出](evidence/t21-test-output.txt)、[嵌入 PNG](evidence/t21-frame-embedded.png)、[独立 PNG](evidence/t21-frame-standalone.png) 均来自最终全量运行，图片已人工查看。

| 入口 | 首次观察到蓝色帧 | 最小 / 中位 / P95 / 最大输入到帧（20 次） |
| --- | --- | --- |
| 嵌入 Session/View | 156.89 ms | 12.84 / 14.23 / 22.33 / 30.05 ms |
| 公开 `termole -e` | 220.66 ms | 14.38 / 17.27 / 31.47 / 31.78 ms |

## 回归与测试条件修正

评审修正后的最终 `build.cmd`（configure、全量构建、CTest）：**42/42 通过，228.34 s**，日志 `.local/t21-reviewed-build.log`。覆盖现有 Session/View、外部 SDK、CMD/WSL、编辑器/lazygit、双入口 fzf 像素交互、鼠标/粘贴、资源压力及 launcher/真实管道。PowerShell 脚本语法检查与 `git diff --check` 通过；仓库没有单独配置的 formatter 检查。

此前的失败与对应处理保留如下，不把重跑结果冒充首轮通过：

- 初始受限执行中 pipe 用例超时、剪贴板调用失败、WSL 返回 `E_ACCESSDENIED`，外部 SDK 编译报 C1902；该轮在视觉用例卡住后被结束。同一套代码改用正常桌面权限后这些用例通过；没有通过修改产品来绕过权限限制。
- 首次正常桌面全量为 41/42：空闲计数 1→2；单独重跑通过。View 本来就会在获得/失去焦点时失效重绘，旧用例主动聚焦却没有控制前台。改为未激活的空闲窗口并保持消息处理，单次及连续五次通过，最终全量也通过；没有放宽绘制计数断言。
- 下一轮 41/42：独立 fzf 的首个尺寸读到 984×601，而实际捕获是 712×468，旧底部行号失配。现在首次主屏截图后重读实际 client 尺寸，再映射 fzf 行；像素阈值不变。[失败时实际可见的候选](evidence/t21-fzf-stale-grid.png) 已保留。
- 单项复核同时确认 `NO_COLOR=1` 会使选中行 strong pixels 为 775，低于既有 800 阈值；有颜色的既有成功样本为 931。三项 fzf 视觉用例通过 CTest `ENVIRONMENT_MODIFICATION` 明确 unset 该变量，仅作用于各测试子进程。带该变量的环境中单项通过，随后全量通过；没有加入 `--no-color` 或降低阈值。
- 两轴评审发现旧汇总把 20 个排序样本的第 11 项误标为中位数。现取第 10、11 项的平均，并重新运行全量、替换本报告的逐帧证据与统计；没有把两轮样本之间的变化当作性能优化收益。

这些修正属于测量/回归夹具，不改变终端行为。

负向检查：将不会绘制该协议的 `cli-report-helper.exe` 作为 latency 目标，实际返回 1，报告初始蓝色和 20 帧缺失，并保存失败截图（`.local/t21-negative/`、`.local/t21-negative.log`）。窗口存在或空白图像不会自动通过。

## 运行中出现的异常与限制

初版脚本的吞吐尝试出现间歇性 launcher 状态 `0xC0000005`，同时托管 GUI ExitCode 为 0；这些失败不纳入性能表，也没有当作通过。现存本机转储的异常流为 `0xC0000008`，线程栈可见 `ntdll`、`KERNELBASE` 和 `amdxx64.dll` 的句柄关闭路径；未获得符号栈或足以区分终止异常与先前异常的证据，不能归因为驱动或 Termole，也不能宣称已修复。

脚本现在保留进程句柄并核对 Win32 GetExitCodeProcess，避免仅依赖托管查询；该改动后独立吞吐连续 30 次及最终正常桌面环境的 5 次通过。产品二进制未改，因此这些成功不证明先前异常消失；失败时脚本仍停止，旧失败和本机转储保持可调查。初始受限执行中 Application 事件日志读取被拒绝；未修改驱动、注册表或系统诊断配置。后续若再次出现，应先用下面的短命令复现，按原生退出状态和转储继续定位，不能只挑成功样本宣称稳定。

## 验收对应

| #22 验收项 | 本地结果与证据 |
| --- | --- |
| 空闲按需刷新、未使用能力不额外初始化、更新可见 | 未激活窗口 5 s 空闲计数稳定、输出触发 paint；无 View / 有 View 内存记录与延迟 factory 的代码检查；双入口 20 帧像素检查。焦点变化本身应触发绘制，不属于空闲。 |
| 启动、CPU、内存、吞吐、帧延迟有方法和基线 | 15 次独立入口 JSON、环境/哈希、逐帧输出和 PNG；分别说明进程冷启动/重复启动与未控制的 OS 缓存，记录观察峰值和端到端延迟代理。 |
| 只依据数据优化，并回归双入口与选择器 | 未改产品实现；现有双入口/选择器、SDK、输入及管道回归随 42/42 全量通过，修复实测暴露的测试条件问题。 |
| 无未经验证的绝对承诺，可复现 | 固定负载、公开入口、项目内脚本、原始样本与限制均列出；不宣称 Windows 10、严格机器冷缓存或所有驱动已经验证。 |

本地交付已备齐；未发布提交、更新 Issue 或关闭 #22，遵守本轮“不发布”的范围。

另外尚未覆盖：重启后的 OS 冷缓存、长期驻留、Windows 10、其他 GPU/驱动、真实硬件按键到显示器的延迟。真实 IME 与发布矩阵仍归 T09/T24。fzf 背压和字节协议仍归 T16/T26；本票回归已有 fzf 视觉交互，不恢复旧 picker。

## 复现

在空闲且可交互的 Windows 桌面运行；不要同时运行其他视觉用例或遮挡探针窗口。

```powershell
.\build.cmd
.\scripts\measure-performance.ps1
# 只复现吞吐或检查异常，失败即停止
.\scripts\measure-performance.ps1 -Kinds throughput -Iterations 20 -Output .local/t21-repeat
.\scripts\test-one.cmd termole_idle_paint_test t21_idle_paint
.\scripts\test-one.cmd termole_visual_test visual_t21
```

`scripts/measure-performance.ps1` 默认输出 `.local/t21-performance/{measurements,environment}.json`；不清缓存、不改系统环境、不读用户配置、不访问网络。原始失败日志和转储不入库。产品与测试工具均来自同一次项目 Release 构建。
