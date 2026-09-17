#include "thread_pool/ThreadPool.h"

#include <iostream>
#include <chrono>
#include <atomic>
#include <cstddef>
#include <string>


int main(int argc, char* argv[])
{
    const std::size_t taskCount = argc > 1
        ? std::stoull(argv[1])
        : 1000000;

    if (taskCount == 0)
    {
        std::cerr << "task count must be greater than 0\n";
        return 1;
    }


    ThreadPool pool(8, taskCount);


    std::atomic<size_t> counter{0};


    auto start =
        std::chrono::high_resolution_clock::now();


    for(size_t i = 0; i < taskCount; i++)
    {
        pool.submit(10, [&counter](){
            counter.fetch_add(
                1,
                std::memory_order_relaxed
            );

        });
    }


    pool.waitIdle();


    auto end =
        std::chrono::high_resolution_clock::now();


    auto duration =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(end - start);


    std::cout
        << "tasks: "
        << taskCount
        << "\n";


    std::cout
        << "time: "
        << duration.count()
        << " ms\n";


    std::cout
        << "throughput: "
        << taskCount * 1000.0 /
           duration.count()
        << " tasks/sec\n";


    std::cout
        << "counter: "
        << counter
        << std::endl;

    return counter == taskCount ? 0 : 1;
}