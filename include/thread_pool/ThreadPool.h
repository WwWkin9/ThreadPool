#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace threadpool {
	inline constexpr int AgingRate = 2;

	enum class TaskType {
		High,
		Normal
	};

	class ThreadPool {
	public:
		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;

		ThreadPool();
		explicit ThreadPool(
			std::size_t threadCount,
			std::size_t maxQueueSize = 1000,
			std::size_t reservedHighWorkers = 0);
		~ThreadPool();

		template<typename F, typename... Args>
		auto submit(int priority, F&& f, Args&&... args)
			-> std::future<std::invoke_result_t<F, Args...>>
		{
			return submit(
				TaskType::Normal,
				priority,
				std::forward<F>(f),
				std::forward<Args>(args)...);
		}

		template<typename F, typename... Args>
		auto submit(TaskType taskType, int priority, F&& f, Args&&... args)
			-> std::future<std::invoke_result_t<F, Args...>>
		{
			using ReturnType = std::invoke_result_t<F, Args...>;

			auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...));
			auto result = packagedTask->get_future();

			enqueue(Task{
				taskType,
				priority,
				std::chrono::steady_clock::now(),
				[packagedTask]() { (*packagedTask)(); }
			});

			return result;
		}

		template<typename Rep, typename Period, typename F, typename... Args>
		auto submitFor(
			int priority,
			const std::chrono::duration<Rep, Period>& timeout,
			F&& f,
			Args&&... args)
			-> std::future<std::invoke_result_t<F, Args...>>
		{
			return submitFor(
				TaskType::Normal,
				priority,
				timeout,
				std::forward<F>(f),
				std::forward<Args>(args)...);
		}

		template<typename Rep, typename Period, typename F, typename... Args>
		auto submitFor(
			TaskType taskType,
			int priority,
			const std::chrono::duration<Rep, Period>& timeout,
			F&& f,
			Args&&... args)
			-> std::future<std::invoke_result_t<F, Args...>>
		{
			using ReturnType = std::invoke_result_t<F, Args...>;

			auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...));
			auto result = packagedTask->get_future();

			enqueueFor(Task{
				taskType,
				priority,
				std::chrono::steady_clock::now(),
				[packagedTask]() { (*packagedTask)(); }
			}, timeout);

			return result;
		}

		void waitIdle();

	private:
		struct Task {
			TaskType type;
			std::int64_t sortKey;
			std::uint64_t sequenceNumber;
			std::function<void()> function;

			Task(
				TaskType taskType,
				int priority,
				std::chrono::steady_clock::time_point enqueueTime,
				std::function<void()> taskFunction)
				: type(taskType),
					sortKey(calculateSortKey(priority, enqueueTime)),
					sequenceNumber(0),
					function(std::move(taskFunction))
			{
			}

			static std::int64_t calculateSortKey(
				int priority,
				std::chrono::steady_clock::time_point enqueueTime) {
				const auto enqueueSeconds = std::chrono::duration_cast<std::chrono::seconds>(
					enqueueTime.time_since_epoch()).count();
				return static_cast<std::int64_t>(priority)
					- static_cast<std::int64_t>(threadpool::AgingRate) * enqueueSeconds;
			}
		};

		struct TaskCompare {
			bool operator()(const Task& left, const Task& right) const {
				if (left.sortKey != right.sortKey) {
					return left.sortKey < right.sortKey;
				}
				return left.sequenceNumber > right.sequenceNumber;
			}
		};

		class TaskQueue {
		public:
			void push(Task task) {
				tasks_.push_back(std::move(task));
				std::push_heap(tasks_.begin(), tasks_.end(), TaskCompare{});
			}

			Task pop() {
				std::pop_heap(tasks_.begin(), tasks_.end(), TaskCompare{});
				Task task = std::move(tasks_.back());
				tasks_.pop_back();
				return task;
			}

			bool empty() const {
				return tasks_.empty();
			}

			std::size_t size() const {
				return tasks_.size();
			}

			void reserve(std::size_t capacity) {
				tasks_.reserve(capacity);
			}
		private:
			std::vector<Task> tasks_;
		};

		void enqueue(Task task);

		template<typename Rep, typename Period>
		void enqueueFor(Task task, const std::chrono::duration<Rep, Period>& timeout) {
			const TaskType taskType = task.type;
			std::unique_lock<std::mutex> lock(mtx_);
			const bool ready = notFullCv_.wait_for(lock, timeout, [this]() {
				return stop_ || hasQueueCapacityLocked();
			});

			if (stop_) {
				throw std::runtime_error("submit on stopped ThreadPool");
			}
			if (!ready) {
				throw std::runtime_error("ThreadPool submit timeout");
			}

			pushTaskLocked(std::move(task));
			lock.unlock();
			notifyTaskAvailable(taskType);
		}

		bool hasQueueCapacityLocked() const;
		bool hasTaskForWorkerLocked(bool highOnly) const;
		bool isIdleLocked() const;
		void pushTaskLocked(Task&& task);
		void notifyTaskAvailable(TaskType taskType);
		Task popNextTaskLocked();
		void workerLoop(bool highOnly);
		void shutdown();

		std::vector<std::thread> workers_;
		TaskQueue highTasks_;
		TaskQueue normalTasks_;
		std::mutex mtx_;
		bool stop_ = false;
		std::size_t maxQueueSize_;
		std::size_t activeTasks_ = 0;
		std::uint64_t nextSequenceNumber_ = 0;
		std::condition_variable highCv_;
		std::condition_variable normalCv_;
		std::condition_variable notFullCv_;
		std::condition_variable idleCv_;
	};
}
