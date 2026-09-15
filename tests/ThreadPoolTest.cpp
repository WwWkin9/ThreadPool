#include "thread_pool/ThreadPool.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>

int main() {
	ThreadPool pool(2, 4);

	auto value = pool.submit([](int left, int right) {
		return left + right;
	}, 2, 3);
	assert(value.get() == 5);
	std::cout << "submit test passed" << std::endl;

	auto delayed = pool.submitFor(std::chrono::seconds(1), [] {
		return 42;
	});
	assert(delayed.get() == 42);
	std::cout << "submitFor test passed" << std::endl;

	pool.waitIdle();

	bool invalidThreadCount = false;
	try {
		ThreadPool invalidPool(0);
	} catch (const std::invalid_argument&) {
		invalidThreadCount = true;
	}
	assert(invalidThreadCount);
	std::cout << "thread count validation passed" << std::endl;

	bool invalidQueueSize = false;
	try {
		ThreadPool invalidPool(1, 0);
	} catch (const std::invalid_argument&) {
		invalidQueueSize = true;
	}
	assert(invalidQueueSize);
	std::cout << "queue size validation passed" << std::endl;
	std::cout << "all tests passed" << std::endl;

	return 0;
}