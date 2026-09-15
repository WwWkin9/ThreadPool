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

class ThreadPool {
public:
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	explicit ThreadPool(std::size_t threadCount = 4, std::size_t maxQueueSize = 1000);

	template<typename F, typename... Args>
	auto submit(F&& f, Args&&... args)
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
			tasks_.push([task]() {
				(*task)();
			});
		}

		notEmptyCv_.notify_one();
		return result;
	}

	template<typename Rep, typename Period, typename F, typename... Args>
	auto submitFor(const std::chrono::duration<Rep, Period>& timeout, F&& f, Args&&... args)
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
			tasks_.push([task]() {
				(*task)();
			});
		}

		notEmptyCv_.notify_one();
		return result;
	}

	void waitIdle();
	~ThreadPool();

private:
	std::vector<std::thread> workers_;
	std::queue<std::function<void()>> tasks_;
	std::mutex mtx_;
	bool stop_ = false;
	std::size_t maxQueueSize_;
	std::size_t activeTasks_;
	std::condition_variable notEmptyCv_;
	std::condition_variable notFullCv_;
	std::condition_variable idleCv_;
};