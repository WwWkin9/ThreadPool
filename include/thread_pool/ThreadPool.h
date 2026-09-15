#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#define AGING_RATE 2

struct Task
{
	int priority;
	std::chrono::steady_clock::time_point enqueue_time;
	std::function<void()> function;

	auto effectivepriority() const {
		auto waitTime = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now() - enqueue_time
		).count();

		return priority + waitTime * AGING_RATE;
	}
};


class ThreadPool {
public:
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	explicit ThreadPool(std::size_t threadCount = 4, std::size_t maxQueueSize = 1000);

	template<typename F, typename... Args>
	auto submit(int priority, F&& f, Args&&... args)
		-> std::future<std::invoke_result_t<F, Args...>>
	{
		using ReturnType = std::invoke_result_t<F, Args...>;

		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...)
		);

		std::future<ReturnType> result = task->get_future();

		{
			std::unique_lock<std::mutex> lock(mtx_);

			notFullCv_.wait(lock, [this]() {
				return stop_ || tasks_.size() < maxQueueSize_;
			});

			if (stop_) {
				throw std::runtime_error("submit on stopped ThreadPool");
			}

			Task t;
			t.priority = priority;
			t.function = [task](){
				(*task)();
			};
			t.enqueue_time = std::chrono::steady_clock::now();

			tasks_.push_back(std::move(t));
		}

		notEmptyCv_.notify_one();
		return result;
	}

	template<typename Rep, typename Period, typename F, typename... Args>
	auto submitFor(int priority, const std::chrono::duration<Rep, Period>& timeout, F&& f, Args&&... args)
		-> std::future<std::invoke_result_t<F, Args...>>
	{
		using ReturnType = std::invoke_result_t<F, Args...>;

		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...)
		);

		std::future<ReturnType> result = task->get_future();

		{
			std::unique_lock<std::mutex> lock(mtx_);

			const bool ready = notFullCv_.wait_for(lock, timeout, [this]() {
				return stop_ || tasks_.size() < maxQueueSize_;
			});

			if (stop_) {
				throw std::runtime_error("submit on stopped ThreadPool");
			}
			if (!ready) {
				throw std::runtime_error("ThreadPool submit timeout");
			}

			Task t;
			t.priority = priority;
			t.function = [task](){
				(*task)();
			};
			t.enqueue_time = std::chrono::steady_clock::now();

			tasks_.push_back(std::move(t));
		}

		notEmptyCv_.notify_one();
		return result;
	}

	void waitIdle();
	Task popBestTask();
	~ThreadPool();

private:
	std::vector<std::thread> workers_;
	std::vector<Task> tasks_;
	std::mutex mtx_;
	bool stop_ = false;
	std::size_t maxQueueSize_;
	std::size_t activeTasks_;
	std::condition_variable notEmptyCv_;
	std::condition_variable notFullCv_;
	std::condition_variable idleCv_;
};
