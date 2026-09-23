# ThreadPool

一个基于 C++17 的固定大小线程池，使用 CMake 构建，适合需要异步提交任务并通过 `std::future` 获取结果的场景。

## 特性

- 提交任意可调用对象并获取 `std::future`
- 限制等待队列大小，避免无限制积压任务
- 按优先级执行等待任务，优先级数值越大越优先
- 任务等待时间每增加一秒，有效优先级增加 `2`，可避免低优先级任务长期得不到执行
- 支持带超时的任务提交
- 支持为高优先级任务保留专用工作线程
- 支持等待线程池处理完所有运行中和排队中的任务

## 环境要求

- C++17 编译器
- CMake 3.16 或更高版本
- 支持 C++ 线程库的平台
- 运行测试时需要联网下载 GoogleTest 1.18.0（首次配置）

## 构建

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

如果只需要构建库，可以关闭测试目标和 GoogleTest 依赖：

```powershell
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --config Release
```

在 VS Code 中可以运行工作区任务 `CMake: Test ThreadPool`。本项目是 C++/CMake 项目，不包含 Go 模块。

启用 benchmark：

```powershell
cmake -S . -B build -DBUILD_BENCHMARK=ON
cmake --build build --config Release --target ThreadPoolBenchmark
build\Release\ThreadPoolBenchmark.exe 100000 1000
```

benchmark 的第一个命令行参数是任务数量，默认为 `100000`；第二个参数是每个任务的计算迭代次数，默认为 `1000`。程序会分别测试空任务调度和 CPU 计算任务，并比较 1、2、4、8 个工作线程与串行基线的耗时、吞吐量和加速比；最后还会在相同任务键下对比 `vector + max_element + erase` 和 `priority_queue + top + pop` 的优先级选择耗时。启用 `BUILD_TESTING` 时，CTest 会额外注册一个使用 `1000` 个任务的低成本 benchmark smoke test。

## 测试

测试使用 GoogleTest 1.18.0，并通过 CTest 自动发现。当前测试覆盖提交结果、优先级顺序、参数校验、异常传递、并发执行、队列边界、空闲等待、并发生产者和析构时清空队列等行为。

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Debug -R ThreadPool.QueueTimeout --output-on-failure
```

最后一条命令只运行队列超时测试。启用 benchmark 后，可以使用 `ctest --test-dir build -C Release -R ThreadPool.Benchmark --output-on-failure` 单独运行 benchmark smoke test。也可以直接运行 `build/Debug/ThreadPoolTest.exe` 执行全部测试。项目的持续集成会在 Windows 和 Linux 上分别验证 Debug、Release 构建。

## 快速开始

```cpp
#include "thread_pool/ThreadPool.h"

ThreadPool pool(4, 1000);
auto result = pool.submit(10, [](int value) {
    return value * 2;
}, 21);

int answer = result.get();
```

## API 说明

### 创建线程池

```cpp
ThreadPool pool;                         // 使用硬件并发数，队列上限为 1000
ThreadPool pool(4);                      // 4 个工作线程，队列上限为 1000
ThreadPool pool(4, 100);                 // 4 个工作线程，最多等待 100 个任务
ThreadPool pool(4, 100, 1);              // 保留 1 个只执行 High 任务的线程
```

构造函数参数依次为：

1. `threadCount`：工作线程数量，必须大于 `0`。
2. `maxQueueSize`：等待队列的最大容量，必须大于 `0`；正在执行的任务不计入此容量。
3. `reservedHighWorkers`：只执行 `TaskType::High` 任务的工作线程数量，必须小于 `threadCount`。

### 提交任务

```cpp
auto normal = pool.submit(10, [] { return 42; });
auto high = pool.submit(TaskType::High, 100, [] { return 7; });
auto withArgs = pool.submit(5, [](int left, int right) {
    return left + right;
}, 2, 3);
```

`submit` 返回 `std::future`。任务函数抛出的异常会保存到 future 中，并在调用 `get()` 时重新抛出。可调用对象和参数支持移动语义，包括只移动类型。

如果不需要返回值或通过 future 获取异常，可以使用 `post`。它只负责提交任务，不创建 `std::future`；`post` 任务抛出的异常不会传播到调用方：

```cpp
pool.post(10, [](int value) {
    // 处理后台任务
}, 42);
```

当等待队列已满时，`submit` 会阻塞，直到队列出现空位或线程池开始停止。线程池停止后继续提交会抛出 `std::runtime_error`。

### 带超时提交

```cpp
using namespace std::chrono_literals;

auto result = pool.submitFor(10, 100ms, [] { return 42; });
```

`submitFor` 的第二个参数是等待队列空位的超时时长。超时前成功入队则返回 future；队列持续满载则抛出 `std::runtime_error`，错误信息为 `ThreadPool submit timeout`。零时长可用于尝试立即入队。

### 高优先级任务和优先级老化

默认任务类型是 `TaskType::Normal`。`TaskType::High` 任务拥有独立队列，并会优先于普通任务执行。设置 `reservedHighWorkers` 后，对应数量的工作线程只消费高优先级队列；其余线程会先处理高优先级任务，再处理普通任务。

同一队列中的任务按照有效优先级选择。有效优先级为提交时的优先级加上等待时间带来的老化值，因此较早排队的低优先级任务最终也能获得执行机会。

### 等待空闲和生命周期

```cpp
pool.waitIdle();
```

`waitIdle()` 会等待所有排队任务和运行中任务完成，但线程池仍然可以继续接收新任务。`ThreadPool` 不支持复制。

线程池会在析构时停止接收新任务，并等待正在运行和已排队的任务完成。`ThreadPool` 不提供公开的提前关闭接口，因此销毁前应先停止所有生产线程，并确保已提交的任务最终能够结束。

## 项目结构

```text
include/thread_pool/ThreadPool.h  公共 API
src/ThreadPool.cpp                线程池实现
tests/ThreadPoolTest.cpp          GoogleTest 测试
benchmark/                        基准测试
```

## 许可证

MIT License
