# ADR 0008：libghostty-vt + MinGW + C 核心

- 状态：accepted
- 日期：2026-11-19
- 取代：[ADR 0005](0005-minimal-vt-core.md)
- 相关：ADR 0002（宿主能力）、ADR 0006（execute/stdio/pipe 边界）、ADR 0007（launcher）

## 背景

首版切片使用项目内最小 VT 核心（ADR 0005），后续需要 CJK/组合字符、备用屏幕、鼠标、bracketed paste、选择、回滚与更完整的 OSC/查询支持。继续扩展自研核心的成本已超过引入成熟实现。

libghostty-vt 1.0.1 提供独立 C ABI、Windows x86_64 gnu 预编译静态库；它不依赖 GUI，适合本项目的 Win32 外壳。其静态库不能被 MSVC link.exe 链接，但可被 MinGW/clang 链接。

## 决策

1. 使用 libghostty-vt 1.0.1（`ghostty-vt-1.0.1-x86_64-windows-gnu.zip`）作为终端语义引擎，vendored 到 `third_party/libghostty-vt/1.0.1/`，固定 SHA-256。
2. 新增 C ABI `include/termole/core.h`（frame/cell/模式/标题/滚动/选择/文本/回调），实现位于 `src/vt/termole_vt.c`；C++ `NativeSession` 只作为适配层。
3. 产品工具链改为 MinGW-w64 gcc/g++ 16；CMake 使用 **MSYS Makefiles**，不依赖 Ninja。
4. 默认静态链接：`-static`、`-static-libgcc -static-libstdc++`；产品不随包分发 MinGW 运行库。
5. 公共 `termole.h` ABI v3 保持不变；旧 `src/core/terminal.*` 删除，`Cell`/`Snapshot` 移至 `src/core/screen.h`。
6. 回滚逻辑上限保持 1000 行：ghostty 后端设为 2000，适配层映射最近 1000 行，以吸收页面粒度。

## 后果

- 终端行为由 libghostty-vt 提供；VT 单元测试改为 adapter contract + 集成/视觉测试。
- C++ 仍承担 `NativeSession`、View/Renderer、apps；后续按计划继续 C 化。
- 构建必须从 MSYS2 bash 运行；宿主 CMake ≥3.28。
- libghostty-vt 以 MPL-2.0 分发，未修改上游源码；来源和校验记录在 `third_party/libghostty-vt/SOURCE.txt`。

## 验证

迁移完成时：

```
100% tests passed out of 33
（visual 6/6；WSL 按宿主策略跳过）
```

C-only 宿主示例与 `termole.h` v3 ABI 测试通过。
