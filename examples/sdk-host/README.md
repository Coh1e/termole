# SDK 宿主示例

在解压后的开发包根目录打开 x64 Native Tools Command Prompt，将 MinGW-w64 的 bin 目录加入 PATH。

```bat
cmake -S share/termole/examples/sdk-host -B .local/sdk-host -G "NMake Makefiles" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-static "-DCMAKE_PREFIX_PATH=%CD%"
cmake --build .local/sdk-host
```

生成的程序为 `.local/sdk-host/termole-sdk-host.exe`。示例通过 `termole.hpp` 使用静态 SDK。宿主管理顶层窗口、消息循环、布局、焦点和关闭顺序，Termole 管理子视图和会话。库不读取用户 TOML，也不弹出对话框。宿主调用 `termole_view_copy_selection` 时，库才向剪贴板写入选区。

从源码仓库构建时，将 `-S` 改为 `examples/sdk-host`，并将 `CMAKE_PREFIX_PATH` 指向开发包根目录。

开发包内的接口手册位于 `docs/sdk.md`。仓库中可查阅 [SDK 手册](https://github.com/Coh1e/termole/blob/main/docs/sdk.md)和[完整调用示例](https://github.com/Coh1e/termole/blob/main/tests/sdk-consumer/main.cpp)。测试 `t22_sdk_consumer` 使用已安装的开发包构建该示例。
