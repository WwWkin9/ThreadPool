#include "thread_pool/ThreadPool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>


using namespace threadpool;
namespace {

using namespace std::chrono_literals;

class Gate {
public:
	Gate() : signal_(promise_.get_future().share()) {}
	Gate(const Gate&) = delete;
	Gate& operator=(const Gate&) = delete;
	~Gate() { open(); }

	std::shared_future<void> signal() const { return signal_; }

	void open() {
		if (!opened_) {
			opened_ = true;
			promise_.set_value();
		}
	}

private:
	std::promise<void> promise_;
	std::shared_future<void> signal_;
	bool opened_ = false;
};

TEST(ThreadPool, BasicSubmission) {
	ThreadPool pool(2, 4);
	auto sum = pool.submit(100, [](int left, int right) { return left + right; }, 2, 3);
	auto timed = pool.submitFor(100, 1s, [] { return 42; });
	auto voidTask = pool.submit(100, [] {});
	EXPECT_EQ(sum.get(), 5);
	EXPECT_EQ(timed.get(), 42);
	EXPECT_NO_THROW(voidTask.get());
	pool.waitIdle();
}

TEST(ThreadPool, DefaultConfiguration) {
	ThreadPool pool;
	EXPECT_EQ(pool.submit(0, [] { return 7; }).get(), 7);
}

TEST(ThreadPool, ReferenceArgument) {
	ThreadPool pool(1, 1);
	int value = 10;
	auto result = pool.submit(0, [](int& target) { return ++target; }, std::ref(value));
	EXPECT_EQ(result.get(), 11);
	EXPECT_EQ(value, 11);
}

TEST(ThreadPool, InvalidConfiguration) {
	EXPECT_THROW({ ThreadPool pool(0); }, std::invalid_argument);
	EXPECT_THROW({ ThreadPool pool(1, 0); }, std::invalid_argument);
	EXPECT_THROW({ ThreadPool pool(2, 4, 2); }, std::invalid_argument);
}

TEST(ThreadPool, HighPriorityClassExecutes) {
	ThreadPool pool(2, 4, 1);
	auto result = pool.submit(TaskType::High, 10, [] { return 42; });
	EXPECT_EQ(result.get(), 42);
}

TEST(ThreadPool, ReservedHighWorkerDoesNotRunNormalTasks) {
	ThreadPool pool(2, 4, 1);
	Gate gate;
	auto normalStarted = std::make_shared<std::promise<void>>();
	auto normalReady = normalStarted->get_future();
	auto runningNormal = pool.submit(0, [normalStarted, signal = gate.signal()] {
		normalStarted->set_value();
		signal.wait();
	});
	ASSERT_EQ(normalReady.wait_for(2s), std::future_status::ready);

	auto queuedNormal = pool.submit(1, [] { return 1; });
	auto high = pool.submit(TaskType::High, 1, [] { return 2; });
	EXPECT_EQ(high.wait_for(2s), std::future_status::ready);
	EXPECT_EQ(queuedNormal.wait_for(100ms), std::future_status::timeout);

	gate.open();
	EXPECT_EQ(high.get(), 2);
	runningNormal.get();
	EXPECT_EQ(queuedNormal.get(), 1);
}

TEST(ThreadPool, TaskException) {
	ThreadPool pool(1, 2);
	auto failed = pool.submit(90, []() -> int { throw std::logic_error("task failed"); });
	EXPECT_THROW(failed.get(), std::logic_error);
	auto timedFailed = pool.submitFor(90, 1s, []() -> int {
		throw std::logic_error("timed task failed");
	});
	EXPECT_THROW(timedFailed.get(), std::logic_error);
	EXPECT_EQ(pool.submit(90, [] { return 7; }).get(), 7);
}

TEST(ThreadPool, MoveOnlyCallable) {
	ThreadPool pool(1, 1);
	auto result = pool.submit(10, [value = std::make_unique<int>(42)] { return *value; });
	EXPECT_EQ(result.get(), 42);
}

TEST(ThreadPool, PostExecutesMoveOnlyCallable) {
	ThreadPool pool(1, 1);
	std::promise<int> completed;
	auto result = completed.get_future();
	pool.post(10, [value = std::make_unique<int>(42), &completed]() mutable {
		completed.set_value(*value);
	});
	EXPECT_EQ(result.get(), 42);
	pool.waitIdle();
}

TEST(ThreadPool, PostTaskExceptionDoesNotStopWorker) {
	ThreadPool pool(1, 2);
	pool.post(10, [] {
		throw std::logic_error("post task failed");
	});
	auto result = pool.submit(10, [] { return 7; });
	EXPECT_EQ(result.get(), 7);
}

TEST(ThreadPool, ConcurrentWorkers) {
	ThreadPool pool(2, 2);
	Gate gate;
	auto firstStarted = std::make_shared<std::promise<void>>();
	auto secondStarted = std::make_shared<std::promise<void>>();
	auto firstReady = firstStarted->get_future();
	auto secondReady = secondStarted->get_future();
	auto first = pool.submit(10, [firstStarted, signal = gate.signal()] {
		firstStarted->set_value();
		signal.wait();
	});
	auto second = pool.submit(20, [secondStarted, signal = gate.signal()] {
		secondStarted->set_value();
		signal.wait();
	});
	const bool bothStarted = firstReady.wait_for(2s) == std::future_status::ready
		&& secondReady.wait_for(2s) == std::future_status::ready;
	gate.open();
	EXPECT_TRUE(bothStarted) << "two workers should run simultaneously";
	first.get();
	second.get();
}

TEST(ThreadPool, HigherPriorityQueuedTasksRunFirst) {
	ThreadPool pool(1, 4);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	auto order = std::make_shared<std::vector<int>>();
	auto running = pool.submit(0, [started, order, signal = gate.signal()] {
		order->push_back(0);
		started->set_value();
		signal.wait();
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
	auto low = pool.submit(-10, [order] { order->push_back(-10); });
	auto high = pool.submit(100, [order] { order->push_back(100); });
	auto medium = pool.submit(20, [order] { order->push_back(20); });
	gate.open();
	running.get();
	low.get();
	high.get();
	medium.get();
	EXPECT_EQ(*order, (std::vector<int>{0, 100, 20, -10}));
}

TEST(ThreadPool, SamePriorityTasksPreserveSubmissionOrder) {
	ThreadPool pool(1, 8);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	std::vector<int> order;

	auto running = pool.submit(0, [started, signal = gate.signal()] {
		started->set_value();
		signal.wait();
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);

	std::vector<std::future<void>> queued;
	for (int value = 1; value <= 4; ++value) {
		queued.push_back(pool.submit(0, [&order, value] {
			order.push_back(value);
		}));
	}

	gate.open();
	running.get();
	for (auto& task : queued) {
		task.get();
	}

	EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4}));
}

TEST(ThreadPool, WaitingTaskGainsPriority) {
	ThreadPool pool(1, 4);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	std::vector<int> order;

	auto running = pool.submit(0, [started, signal = gate.signal()] {
		started->set_value();
		signal.wait();
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);

	auto aged = pool.submit(-1, [&order] {
		order.push_back(-1);
	});
	std::this_thread::sleep_for(1200ms);
	auto newer = pool.submit(0, [&order] {
		order.push_back(0);
	});

	gate.open();
	running.get();
	aged.get();
	newer.get();

	EXPECT_EQ(order, (std::vector<int>{-1, 0}));
}

TEST(ThreadPool, QueueTimeout) {
	ThreadPool pool(1, 1);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	auto running = pool.submit(0, [started, signal = gate.signal()] {
		started->set_value();
		signal.wait();
		return 1;
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
	auto queued = pool.submit(1, [] { return 2; });
	bool timedOut = false;
	try {
		pool.submitFor(2, 100ms, [] { return 3; });
	} catch (const std::runtime_error& error) {
		timedOut = std::string(error.what()) == "ThreadPool submit timeout";
	}
	gate.open();
	EXPECT_TRUE(timedOut) << "submitFor should time out with a full queue";
	EXPECT_EQ(running.get(), 1);
	EXPECT_EQ(queued.get(), 2);
}

TEST(ThreadPool, ZeroTimeoutUsesAvailableCapacity) {
	ThreadPool pool(1, 1);
	EXPECT_EQ(pool.submitFor(0, 0ms, [] { return 7; }).get(), 7);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	auto running = pool.submit(0, [started, signal = gate.signal()] {
		started->set_value();
		signal.wait();
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
	auto queued = pool.submit(1, [] {});
	bool timedOut = false;
	try {
		pool.submitFor(2, 0ms, [] {});
	} catch (const std::runtime_error& error) {
		timedOut = std::string(error.what()) == "ThreadPool submit timeout";
	}
	gate.open();
	EXPECT_TRUE(timedOut);
	running.get();
	queued.get();
}

TEST(ThreadPool, TimedSubmissionResumesWhenQueueDrains) {
	ThreadPool pool(1, 1);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	auto running = pool.submit(0, [started, signal = gate.signal()] {
		started->set_value();
		signal.wait();
		return 1;
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
	auto queued = pool.submit(1, [] { return 2; });
	auto producerStarted = std::make_shared<std::promise<void>>();
	auto producerReady = producerStarted->get_future();
	auto producer = std::async(std::launch::async, [&pool, producerStarted] {
		producerStarted->set_value();
		return pool.submitFor(2, 2s, [] { return 3; });
	});
	const bool startedSubmitting = producerReady.wait_for(2s) == std::future_status::ready;
	const bool blocked = producer.wait_for(100ms) == std::future_status::timeout;
	gate.open();
	EXPECT_TRUE(startedSubmitting);
	EXPECT_TRUE(blocked) << "timed submit should wait with a full queue";
	ASSERT_EQ(producer.wait_for(2s), std::future_status::ready);
	auto third = producer.get();
	EXPECT_EQ(running.get(), 1);
	EXPECT_EQ(queued.get(), 2);
	EXPECT_EQ(third.get(), 3);
}

TEST(ThreadPool, BlockingSubmission) {
	ThreadPool pool(1, 1);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	auto running = pool.submit(0, [started, signal = gate.signal()] {
		started->set_value();
		signal.wait();
		return 1;
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
	auto queued = pool.submit(1, [] { return 2; });
	auto producerStarted = std::make_shared<std::promise<void>>();
	auto producerReady = producerStarted->get_future();
	auto producer = std::async(std::launch::async, [&pool, producerStarted] {
		producerStarted->set_value();
		return pool.submit(2, [] { return 3; });
	});
	const bool startedSubmitting = producerReady.wait_for(2s) == std::future_status::ready;
	const bool blocked = producer.wait_for(100ms) == std::future_status::timeout;
	gate.open();
	EXPECT_TRUE(startedSubmitting);
	EXPECT_TRUE(blocked) << "submit should block with a full queue";
	ASSERT_EQ(producer.wait_for(2s), std::future_status::ready);
	auto third = producer.get();
	EXPECT_EQ(running.get(), 1);
	EXPECT_EQ(queued.get(), 2);
	EXPECT_EQ(third.get(), 3);
}

TEST(ThreadPool, WaitForIdle) {
	ThreadPool pool(1, 1);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	auto running = pool.submit(0, [started, signal = gate.signal()] {
		started->set_value();
		signal.wait();
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
	auto queued = pool.submit(1, [] {});
	auto waiterStarted = std::make_shared<std::promise<void>>();
	auto waiterReady = waiterStarted->get_future();
	auto waiter = std::async(std::launch::async, [&pool, waiterStarted] {
		waiterStarted->set_value();
		pool.waitIdle();
	});
	const bool startedWaiting = waiterReady.wait_for(2s) == std::future_status::ready;
	const bool blocked = waiter.wait_for(100ms) == std::future_status::timeout;
	gate.open();
	EXPECT_TRUE(startedWaiting);
	EXPECT_TRUE(blocked) << "waitIdle should wait for active and queued work";
	ASSERT_EQ(waiter.wait_for(2s), std::future_status::ready);
	waiter.get();
	running.get();
	queued.get();
}

TEST(ThreadPool, WaitForIdleWithOnlyActiveTask) {
	ThreadPool pool(1, 1);
	Gate gate;
	auto started = std::make_shared<std::promise<void>>();
	auto ready = started->get_future();
	auto running = pool.submit(0, [started, signal = gate.signal()] {
		started->set_value();
		signal.wait();
	});
	ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
	auto waiterStarted = std::make_shared<std::promise<void>>();
	auto waiterReady = waiterStarted->get_future();
	auto waiter = std::async(std::launch::async, [&pool, waiterStarted] {
		waiterStarted->set_value();
		pool.waitIdle();
	});
	const bool startedWaiting = waiterReady.wait_for(2s) == std::future_status::ready;
	const bool blocked = waiter.wait_for(100ms) == std::future_status::timeout;
	gate.open();
	EXPECT_TRUE(startedWaiting);
	EXPECT_TRUE(blocked) << "waitIdle should count running work even with an empty queue";
	ASSERT_EQ(waiter.wait_for(2s), std::future_status::ready);
	waiter.get();
	running.get();
}

TEST(ThreadPool, DestructorDrainsQueue) {
	std::future<int> running;
	std::future<int> queued;
	{
		ThreadPool pool(1, 1);
		auto started = std::make_shared<std::promise<void>>();
		auto ready = started->get_future();
		running = pool.submit(0, [started] {
			started->set_value();
			std::this_thread::sleep_for(100ms);
			return 1;
		});
		ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
		queued = pool.submit(1, [] { return 2; });
	}
	EXPECT_EQ(running.get(), 1);
	EXPECT_EQ(queued.get(), 2);
}

TEST(ThreadPool, ConcurrentProducers) {
	ThreadPool pool(4, 8);
	std::atomic<int> executed{0};
	std::vector<std::future<int>> producers;
	for (int producer = 0; producer < 4; ++producer) {
		producers.push_back(std::async(std::launch::async, [&pool, &executed] {
			std::vector<std::future<int>> tasks;
			for (int task = 0; task < 50; ++task) {
				tasks.push_back(pool.submit(task % 5, [&executed] {
					++executed;
					return 1;
				}));
			}
			int total = 0;
			for (auto& task : tasks) {
				total += task.get();
			}
			return total;
		}));
	}
	int total = 0;
	for (auto& producer : producers) {
		total += producer.get();
	}
	pool.waitIdle();
	EXPECT_EQ(total, 200);
	EXPECT_EQ(executed.load(), 200);
}

} // namespace
