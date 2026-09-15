# ThreadPool

一个基于 C++17 的固定大小线程池，支持：

- 提交任意可调用对象并获取 `std::future`
- 限制等待队列大小
- 按优先级执行等待任务（数值越大越优先）
- 带超时的任务提交
- 等待线程池进入空闲状态

## 构建

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

在 VS Code 中请运行工作区任务 `CMake: Test ThreadPool`，不要运行 Go 扩展提供的 `test package` 任务。本项目是 C++/CMake 项目，不包含 Go 模块。

## 测试

`tests/CMakeLists.txt` 固定使用 GoogleTest 1.18.0；首次配置时 CMake 会下载依赖。GoogleTest 自动向 CTest 登记各行为测试，覆盖提交结果、优先级顺序、参数校验、异常传递、并发执行、队列边界、空闲等待和析构清空队列。Debug 和 Release 构建都会执行这些检查。
只需构建库时，可在配置时设置 `-DBUILD_TESTING=OFF`，跳过 GoogleTest 下载。

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Debug -R ThreadPool.QueueTimeout --output-on-failure
```

最后一条命令只运行队列超时测试。也可以直接运行 `build/Debug/ThreadPoolTest.exe` 来执行全部测试。GitHub Actions 会在 Windows 和 Linux 上分别验证 Debug、Release 构建。

## 使用

```cpp
#include "thread_pool/ThreadPool.h"

ThreadPool pool(4, 1000);
auto result = pool.submit(10, [](int value) {
    return value * 2;
}, 21);

int answer = result.get();
```
