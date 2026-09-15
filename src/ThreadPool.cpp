#include "thread_pool/ThreadPool.h"

ThreadPool::ThreadPool(std::size_t threadCount, std::size_t maxQueueSize)
	: maxQueueSize_(maxQueueSize), activeTasks_(0) {
	if (maxQueueSize == 0) {
		throw std::invalid_argument("maxQueueSize must be greater than 0");
	}

	if (threadCount == 0) {
		throw std::invalid_argument("threadCount must be greater than 0");
	}

	for (std::size_t i = 0; i < threadCount; ++i) {
		workers_.emplace_back([this]() {
			while (true) {
				Task task;
				{
					std::unique_lock<std::mutex> lock(mtx_);
					notEmptyCv_.wait(lock, [this]() {
						return stop_ || !tasks_.empty();
					});
					if (stop_ && tasks_.empty()) {
						return;
					}
					task = std::move(tasks_.top());
					tasks_.pop();
					++activeTasks_;
				}

				notFullCv_.notify_one();
				task.function();

				{
					std::lock_guard<std::mutex> lock(mtx_);
					--activeTasks_;
					if (activeTasks_ == 0 && tasks_.empty()) {
						idleCv_.notify_all();
					}
				}
			}
		});
	}
}

void ThreadPool::waitIdle() {
	std::unique_lock<std::mutex> lock(mtx_);
	idleCv_.wait(lock, [this]() {
		return tasks_.empty() && activeTasks_ == 0;
	});
}

ThreadPool::~ThreadPool() {
	{
		std::lock_guard<std::mutex> lock(mtx_);
		stop_ = true;
	}

	notEmptyCv_.notify_all();
	notFullCv_.notify_all();
	for (std::thread& worker : workers_) {
		if (worker.joinable()) {
			worker.join();
		}
	}
}