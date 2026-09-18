#include "thread_pool/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using namespace threadpool;

namespace {

using Clock = std::chrono::steady_clock;

struct Result {
    std::chrono::nanoseconds elapsed;
    std::uint64_t checksum;
};

struct SelectionTask {
    std::int64_t sortKey;
    std::uint64_t sequenceNumber;
};

struct SelectionTaskCompare {
    bool operator()(const SelectionTask& left, const SelectionTask& right) const {
        if (left.sortKey != right.sortKey) {
            return left.sortKey < right.sortKey;
        }
        return left.sequenceNumber > right.sequenceNumber;
    }
};

std::uint64_t doWork(std::size_t iterations, std::uint64_t seed) {
    std::uint64_t value = seed + 1U;
    for (std::size_t i = 0; i < iterations; ++i) {
        value = value * 1664525U + 1013904223U;
        value ^= value >> 13U;
    }
    return value;
}

Result runSerial(std::size_t taskCount, std::size_t workIterations) {
    std::uint64_t checksum = 0;
    const auto start = Clock::now();
    for (std::size_t task = 0; task < taskCount; ++task) {
        checksum += doWork(workIterations, task);
    }
    return {Clock::now() - start, checksum};
}

Result runThreadPool(
    std::size_t taskCount,
    std::size_t workIterations,
    std::size_t workerCount) {
    ThreadPool pool(workerCount, taskCount);
    std::vector<std::uint64_t> results(taskCount);
    const auto start = Clock::now();

    for (std::size_t task = 0; task < taskCount; ++task) {
        pool.submit(10, [&results, task, workIterations]() {
            results[task] = doWork(workIterations, task);
        });
    }
    pool.waitIdle();

    std::uint64_t checksum = 0;
    for (const std::uint64_t result : results) {
        checksum += result;
    }
    return {Clock::now() - start, checksum};
}

Result runThreadPoolPost(
    std::size_t taskCount,
    std::size_t workIterations,
    std::size_t workerCount) {
    ThreadPool pool(workerCount, taskCount);
    std::vector<std::uint64_t> results(taskCount);
    const auto start = Clock::now();

    for (std::size_t task = 0; task < taskCount; ++task) {
        pool.post(10, [&results, task, workIterations]() {
            results[task] = doWork(workIterations, task);
        });
    }
    pool.waitIdle();

    std::uint64_t checksum = 0;
    for (const std::uint64_t result : results) {
        checksum += result;
    }
    return {Clock::now() - start, checksum};
}

SelectionTask makeSelectionTask(std::size_t index) {
    const auto key = static_cast<std::int64_t>((index * 37U) % 1001U)
        - static_cast<std::int64_t>(index / 100U);
    return {key, index};
}

Result runVectorSelection(std::size_t queueSize, std::size_t rounds) {
    std::uint64_t checksum = 0;
    const auto start = Clock::now();
    for (std::size_t round = 0; round < rounds; ++round) {
        std::vector<SelectionTask> tasks;
        tasks.reserve(queueSize);
        for (std::size_t index = 0; index < queueSize; ++index) {
            tasks.push_back(makeSelectionTask(index));
        }
        while (!tasks.empty()) {
            const auto best = std::max_element(
                tasks.begin(),
                tasks.end(),
                [](const SelectionTask& left, const SelectionTask& right) {
                    return SelectionTaskCompare{}(left, right);
                });
            checksum += best->sequenceNumber;
            tasks.erase(best);
        }
    }
    return {Clock::now() - start, checksum};
}

Result runPriorityQueueSelection(std::size_t queueSize, std::size_t rounds) {
    std::uint64_t checksum = 0;
    const auto start = Clock::now();
    for (std::size_t round = 0; round < rounds; ++round) {
        std::priority_queue<
            SelectionTask,
            std::vector<SelectionTask>,
            SelectionTaskCompare> tasks;
        for (std::size_t index = 0; index < queueSize; ++index) {
            tasks.push(makeSelectionTask(index));
        }
        while (!tasks.empty()) {
            checksum += tasks.top().sequenceNumber;
            tasks.pop();
        }
    }
    return {Clock::now() - start, checksum};
}

double tasksPerSecond(std::size_t taskCount, std::chrono::nanoseconds elapsed) {
    return static_cast<double>(taskCount) * 1'000'000'000.0
        / static_cast<double>(elapsed.count());
}

void printResult(
    const std::string& name,
    std::size_t taskCount,
    const Result& result,
    const Result& serial) {
    const double throughput = tasksPerSecond(taskCount, result.elapsed);
    const double speedup = static_cast<double>(serial.elapsed.count())
        / static_cast<double>(result.elapsed.count());
    std::cout << std::left << std::setw(20) << name
        << " time=" << std::setw(10)
        << std::chrono::duration<double, std::milli>(result.elapsed).count()
        << " ms throughput=" << std::setw(12) << throughput
        << " tasks/sec speedup=" << speedup << "x\n";
}

void printSelectionResult(
    std::size_t queueSize,
    const Result& vectorResult,
    const Result& priorityQueueResult) {
    const double vectorMilliseconds =
        std::chrono::duration<double, std::milli>(vectorResult.elapsed).count();
    const double priorityQueueMilliseconds =
        std::chrono::duration<double, std::milli>(priorityQueueResult.elapsed).count();
    const double speedup = static_cast<double>(vectorResult.elapsed.count())
        / static_cast<double>(priorityQueueResult.elapsed.count());
    std::cout << "queue=" << std::setw(6) << queueSize
        << " vector=" << std::setw(10) << vectorMilliseconds << " ms"
        << " priority_queue=" << std::setw(10) << priorityQueueMilliseconds << " ms"
        << " speedup=" << speedup << "x\n";
}

} // namespace

int main(int argc, char* argv[])
{
    const std::size_t taskCount = argc > 1 ? std::stoull(argv[1]) : 100000;
    const std::size_t workIterations = argc > 2 ? std::stoull(argv[2]) : 1000;
    if (taskCount == 0) {
        std::cerr << "task count must be greater than 0\n";
        return 1;
    }

    const Result emptySerial = runSerial(taskCount, 0);
    const Result workSerial = runSerial(taskCount, workIterations);
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const std::size_t maxWorkers = hardwareThreads == 0 ? 8 : hardwareThreads;
    std::vector<std::size_t> workerCounts{1, 2, 4, 8};

    std::cout << "tasks: " << taskCount
        << ", work iterations: " << workIterations
        << ", hardware threads: " << maxWorkers << "\n\n";
    std::cout << "Empty task scheduling:\n";
    for (const std::size_t workerCount : workerCounts) {
        if (workerCount <= maxWorkers) {
            const Result result = runThreadPool(taskCount, 0, workerCount);
            printResult(std::to_string(workerCount) + " workers", taskCount, result, emptySerial);
            if (result.checksum != emptySerial.checksum) {
                return 1;
            }
        }
    }

    std::cout << "\nCPU workload:\n";
    for (const std::size_t workerCount : workerCounts) {
        if (workerCount <= maxWorkers) {
            const Result result = runThreadPool(taskCount, workIterations, workerCount);
            printResult(std::to_string(workerCount) + " workers", taskCount, result, workSerial);
            if (result.checksum != workSerial.checksum) {
                return 1;
            }
        }
    }

    std::cout << "\nCPU workload without future:\n";
    for (const std::size_t workerCount : workerCounts) {
        if (workerCount <= maxWorkers) {
            const Result result = runThreadPoolPost(taskCount, workIterations, workerCount);
            printResult(std::to_string(workerCount) + " workers", taskCount, result, workSerial);
            if (result.checksum != workSerial.checksum) {
                return 1;
            }
        }
    }

    std::cout << "\nPriority selection comparison (3 rounds):\n";
    for (const std::size_t queueSize : {100U, 1000U, 5000U}) {
        const Result vectorResult = runVectorSelection(queueSize, 3);
        const Result priorityQueueResult = runPriorityQueueSelection(queueSize, 3);
        if (vectorResult.checksum != priorityQueueResult.checksum) {
            return 1;
        }
        printSelectionResult(queueSize, vectorResult, priorityQueueResult);
    }

    return 0;
}