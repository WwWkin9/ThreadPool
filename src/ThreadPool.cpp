#include "thread_pool/ThreadPool.h"

#include <algorithm>

ThreadPool::ThreadPool()
	: ThreadPool(
		std::max(1U, std::thread::hardware_concurrency()),
		1000,
		0) {
}

ThreadPool::ThreadPool(
	std::size_t threadCount,
	std::size_t maxQueueSize,
	std::size_t reservedHighWorkers)
	: maxQueueSize_(maxQueueSize) {
	if (threadCount == 0) {
		throw std::invalid_argument("threadCount must be greater than 0");
	}
	if (maxQueueSize == 0) {
		throw std::invalid_argument("maxQueueSize must be greater than 0");
	}
	if (reservedHighWorkers >= threadCount) {
		throw std::invalid_argument("reservedHighWorkers must be less than threadCount");
	}

	workers_.reserve(threadCount);
	try {
		for (std::size_t i = 0; i < threadCount; ++i) {
			const bool highOnly = i < reservedHighWorkers;
			workers_.emplace_back([this, highOnly]() {
				workerLoop(highOnly);
			});
		}
	} catch (...) {
		shutdown();
		throw;
	}
}

ThreadPool::~ThreadPool() {
	shutdown();
}

void ThreadPool::waitIdle() {
	std::unique_lock<std::mutex> lock(mtx_);
	idleCv_.wait(lock, [this]() {
		return isIdleLocked();
	});
}

void ThreadPool::enqueue(Task task) {
	const TaskType taskType = task.type;
	std::unique_lock<std::mutex> lock(mtx_);
	notFullCv_.wait(lock, [this]() {
		return stop_ || hasQueueCapacityLocked();
	});

	if (stop_) {
		throw std::runtime_error("submit on stopped ThreadPool");
	}

	pushTaskLocked(std::move(task));
	lock.unlock();
	notifyTaskAvailable(taskType);
}

bool ThreadPool::hasQueueCapacityLocked() const {
	return highTasks_.size() + normalTasks_.size() < maxQueueSize_;
}

bool ThreadPool::hasTaskForWorkerLocked(bool highOnly) const {
	return !highTasks_.empty() || (!highOnly && !normalTasks_.empty());
}

bool ThreadPool::isIdleLocked() const {
	return highTasks_.empty() && normalTasks_.empty() && activeTasks_ == 0;
}

void ThreadPool::pushTaskLocked(Task&& task) {
	task.sequenceNumber = nextSequenceNumber_++;

	auto& tasks = task.type == TaskType::High ? highTasks_ : normalTasks_;
	tasks.push(std::move(task));
}

void ThreadPool::notifyTaskAvailable(TaskType taskType) {
	if (taskType == TaskType::High) {
		highCv_.notify_one();
	}
	normalCv_.notify_one();
}

ThreadPool::Task ThreadPool::popNextTaskLocked() {
	if (!highTasks_.empty()) {
		Task task = highTasks_.top();
		highTasks_.pop();
		return task;
	}

	Task task = normalTasks_.top();
	normalTasks_.pop();
	return task;
}

void ThreadPool::workerLoop(bool highOnly) {
	auto& taskAvailableCv = highOnly ? highCv_ : normalCv_;

	while (true) {
		Task task;
		{
			std::unique_lock<std::mutex> lock(mtx_);
			taskAvailableCv.wait(lock, [this, highOnly]() {
				return stop_ || hasTaskForWorkerLocked(highOnly);
			});

			if (stop_ && !hasTaskForWorkerLocked(highOnly)) {
				return;
			}

			task = popNextTaskLocked();
			++activeTasks_;
		}

		notFullCv_.notify_one();
		task.function();

		{
			std::lock_guard<std::mutex> lock(mtx_);
			--activeTasks_;
			if (isIdleLocked()) {
				idleCv_.notify_all();
			}
		}
	}
}

void ThreadPool::shutdown() {
	{
		std::lock_guard<std::mutex> lock(mtx_);
		stop_ = true;
	}

	highCv_.notify_all();
	normalCv_.notify_all();
	notFullCv_.notify_all();

	for (auto& worker : workers_) {
		if (worker.joinable()) {
			worker.join();
		}
	}
}
