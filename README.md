# ThreadPool

一个基于 C++17 的固定大小线程池，支持：

- 提交任意可调用对象并获取 `std::future`
- 限制等待队列大小
- 带超时的任务提交
- 等待线程池进入空闲状态

## 构建

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

在 VS Code 中请运行工作区任务 `CMake: Test ThreadPool`，不要运行 Go 扩展提供的 `test package` 任务。本项目是 C++/CMake 项目，不包含 Go 模块。

## 使用

```cpp
#include "thread_pool/ThreadPool.h"

ThreadPool pool(4, 1000);
auto result = pool.submit([](int value) {
    return value * 2;
}, 21);

int answer = result.get();
```