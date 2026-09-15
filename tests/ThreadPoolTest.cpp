#include "thread_pool/ThreadPool.h"

#include <cassert>
#include <chrono>
#include <stdexcept>

int main() {
	ThreadPool pool(2, 4);

	auto value = pool.submit([](int left, int right) {
		return left + right;
	}, 2, 3);
	assert(value.get() == 5);

	auto delayed = pool.submitFor(std::chrono::seconds(1), [] {
		return 42;
	});
	assert(delayed.get() == 42);

	pool.waitIdle();

	bool invalidThreadCount = false;
	try {
		ThreadPool invalidPool(0);
	} catch (const std::invalid_argument&) {
		invalidThreadCount = true;
	}
	assert(invalidThreadCount);

	bool invalidQueueSize = false;
	try {
		ThreadPool invalidPool(1, 0);
	} catch (const std::invalid_argument&) {
		invalidQueueSize = true;
	}
	assert(invalidQueueSize);

	return 0;
}