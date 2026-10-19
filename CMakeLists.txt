cmake_minimum_required(VERSION 3.16)
project(LogForge VERSION 1.0.0 LANGUAGES CXX)

# Enforce C++17 Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings and Optimization Options
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Sanitizer Options
option(ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be combined.")
endif()

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) enabled")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer (TSan) enabled")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

# Threads Dependency
find_package(Threads REQUIRED)

# Core Library Sources
set(LOGFORGE_SOURCES
    src/tokenizer.cpp
    src/index.cpp
    src/topk.cpp
    src/diff.cpp
    src/io/read_reader.cpp
    src/io/mmap_reader.cpp
    src/concurrency/pipeline.cpp
    src/persistence/serializer.cpp
    src/persistence/durable_writer.cpp
    src/ipc/process_pool.cpp
)

add_library(logforge_lib STATIC ${LOGFORGE_SOURCES})
target_include_directories(logforge_lib PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(logforge_lib PUBLIC Threads::Threads)

# 1. Main CLI Binary
add_executable(indexer src/main.cpp)
target_link_libraries(indexer PRIVATE logforge_lib)

# 2. Test Suite Executable
set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_tokenizer.cpp
    tests/test_index.cpp
    tests/test_topk.cpp
    tests/test_diff.cpp
    tests/test_bounded_queue.cpp
    tests/test_io.cpp
    tests/test_persistence.cpp
    tests/test_pipeline.cpp
)

add_executable(indexer_tests ${TEST_SOURCES})
target_include_directories(indexer_tests PRIVATE tests)
target_link_libraries(indexer_tests PRIVATE logforge_lib)

enable_testing()
add_test(NAME AllUnitTests COMMAND indexer_tests)

# 3. Benchmark Harness Executable
add_executable(indexer_bench bench/benchmark.cpp)
target_link_libraries(indexer_bench PRIVATE logforge_lib)
