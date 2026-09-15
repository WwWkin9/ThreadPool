cmake_minimum_required(VERSION 3.16)

project(ThreadPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(thread_pool src/ThreadPool.cpp)
target_include_directories(thread_pool
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
	target_compile_options(thread_pool PRIVATE /W4)
else()
	target_compile_options(thread_pool PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(ThreadPoolTest tests/ThreadPoolTest.cpp)
target_link_libraries(ThreadPoolTest PRIVATE thread_pool)

add_test(NAME ThreadPoolTest COMMAND ThreadPoolTest)