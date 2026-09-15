cmake_minimum_required(VERSION 3.16)

project(ThreadPool LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(thread_pool src/ThreadPool.cpp)
target_include_directories(thread_pool
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(thread_pool PUBLIC Threads::Threads)

if (MSVC)
	target_compile_options(thread_pool PRIVATE /W4)
else()
	target_compile_options(thread_pool PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if (BUILD_TESTING)
	add_subdirectory(tests)
endif()
