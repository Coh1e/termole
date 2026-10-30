# 性能基线（T21 初步）

本文件记录本机可复现的测量，不是产品承诺。环境：Windows x64、Release、静态 CRT、`termole.exe` 326,656 B、`termole.dll` 325,632 B、`termole-host.exe` 123,392 B。使用真实 ConPTY 与受控子程序。

## 命令

```powershell
# 启动（目标立即退出，close-on-exit 返回）
$app = '.local\build\windows\bin\termole.exe'
$helper = '.local\build\windows\bin\cli-report-helper.exe'
Measure-Command { Start-Process $app -ArgumentList '--close-on-exit','--',$helper,'--exit-code','0' -Wait }

# 空闲 CPU / 内存（默认 PowerShell，5 秒采样）
$p = Start-Process $app -PassThru
Start-Sleep 3
$cpu1 = $p.TotalProcessorTime.TotalMilliseconds
$ws = $p.WorkingSet64
Start-Sleep 5
$p.Refresh()
$cpu2 = $p.TotalProcessorTime.TotalMilliseconds
"$($cpu2-$cpu1) ms CPU, $($ws/1MB) MB working set"

# 输出吞吐（50,000 行）
Start-Process $app -ArgumentList '--close-on-exit','--',$helper,'--lines','50000' -Wait
```

## 结果（本机 2026-10-30）

| 指标 | 结果 |
| --- | --- |
| 冷启动到目标退出 | 1,043 ms |
| 热启动到目标退出（4 次） | 1,007–1,020 ms |
| 空闲 5 秒 CPU | 0 ms（采样分辨率内） |
| 空闲 working set | 51.6 MB |
| 50,000 行输出到退出 | 2,039 ms |

启动包含 Termole 进程、ConPTY、目标子程序和退出清理；不是单看 DLL 加载。空闲期间没有持续重绘或定时器重绘，100 ms 定时器只检查目标/退出状态。

## 结论

- 空闲 CPU 没有可测增长，当前 100 ms 定时器不触发重绘。
- 当前启动约 1 秒，主要成本在进程/ConPTY/目标启动；静态链接和零 fetched 依赖避免了加载时依赖解析。
- 50k 行/2 秒是文本吞吐代理；尚未测帧延迟、峰值内存、选择器大结果背压。
- 未设置绝对性能承诺；后续 T21/T24 需要固定机器、冷缓存和更多重复。

## 缺口

- 帧延迟、键盘到屏幕延迟、峰值内存未测。
- picker 大输入/结果背压、异常关闭下的资源趋势未测。
- 未在 Windows 10 或无开发工具环境复测。
