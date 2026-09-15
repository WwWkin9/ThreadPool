include(FetchContent)

if (POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(
	googletest
	URL https://codeload.github.com/google/googletest/zip/refs/tags/v1.18.0
	URL_HASH SHA256=63b9c77751a5b8f492486005f67533fdc58682b67476fcb91650be5958d5195a
)

# Keep GoogleTest on the same MSVC runtime as the parent project.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(ThreadPoolTest ThreadPoolTest.cpp)
target_link_libraries(ThreadPoolTest PRIVATE thread_pool GTest::gtest_main)
set_target_properties(ThreadPoolTest PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if (MSVC)
	target_compile_options(ThreadPoolTest PRIVATE /W4)
else()
	target_compile_options(ThreadPoolTest PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(ThreadPoolTest PROPERTIES TIMEOUT 10)
