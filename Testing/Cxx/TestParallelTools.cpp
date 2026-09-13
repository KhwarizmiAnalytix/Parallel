/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Comprehensive unit tests for parallel_tools public API
 *
 * Tests cover:
 * - Thread initialization and configuration
 * - Parallel for execution with various functors
 * - Nested parallelism control
 * - Parallel scope detection
 * - Local scope configuration
 * - Edge cases and error handling
 * - Backend-agnostic functionality
 */

#include <algorithm>
#include <atomic>
#include <numeric>
#include <thread>
#include <vector>

#include "ParallelTest.h"
#include "tools/parallel_tools.h"

namespace parallel
{

// ============================================================================
// Test Fixtures and Helper Classes
// ============================================================================

/**
 * @brief Simple functor for testing parallel_for
 */
class simple_functor
{
public:
    std::vector<std::atomic<int>>& data_;

    simple_functor(std::vector<std::atomic<int>>& data) : data_(data) {}

    void operator()(size_t begin, size_t end)
    {
        for (size_t i = begin; i < end; ++i)
        {
            data_[i].store(static_cast<int>(i * 2), std::memory_order_relaxed);
        }
    }
};

/**
 * @brief Functor with Initialize method
 */
class functor_with_initialize
{
public:
    std::vector<std::atomic<int>>& data_;
    std::atomic<int>&              init_count_;

    functor_with_initialize(std::vector<std::atomic<int>>& data, std::atomic<int>& count)
        : data_(data), init_count_(count)
    {
    }

    void Initialize() { init_count_.fetch_add(1, std::memory_order_relaxed); }

    void operator()(size_t begin, size_t end)
    {
        for (size_t i = begin; i < end; ++i)
        {
            data_[i].store(static_cast<int>(i), std::memory_order_relaxed);
        }
    }

    void Reduce() {}  // Empty reduce - not used in this test
};

/**
 * @brief Const functor for testing const parallel_for
 */
class const_functor
{
public:
    std::atomic<int>& counter_;

    const_functor(std::atomic<int>& counter) : counter_(counter) {}

    void operator()(size_t begin, size_t end) const
    {
        counter_.fetch_add(static_cast<int>(end - begin), std::memory_order_relaxed);
    }
};

PARALLELTEST(ParallelTools, test)
{
    // ============================================================================
    // Consolidated Test 1: Thread Initialization and Configuration
    // ============================================================================

    {
        // Test 1: Initialize with default thread count (0 = auto-detect)
        parallel_tools::initialize(0);
        int num_threads = parallel_tools::estimated_number_of_threads();
        EXPECT_GE(num_threads, 1);
        EXPECT_LE(num_threads, static_cast<int>(std::thread::hardware_concurrency()));

        // Test 2: Initialize with custom thread count
        parallel_tools::initialize(4);
        num_threads = parallel_tools::estimated_number_of_threads();
        EXPECT_GE(num_threads, 1);  // Backend might adjust the count

        // Test 3: Initialize with single thread
        parallel_tools::initialize(1);
        num_threads = parallel_tools::estimated_number_of_threads();
        EXPECT_GE(num_threads, 1);

        // Test 4: Verify estimated_number_of_threads returns reasonable values
        parallel_tools::initialize(0);
        num_threads = parallel_tools::estimated_number_of_threads();
        EXPECT_GT(num_threads, 0);
        EXPECT_LE(num_threads, 1024);  // Reasonable upper bound

        // Test 5: Verify estimated_default_number_of_threads
        int default_threads = parallel_tools::estimated_default_number_of_threads();
        EXPECT_GT(default_threads, 0);
        EXPECT_LE(default_threads, static_cast<int>(std::thread::hardware_concurrency() * 2));

        // Test 6: Multiple initializations should be safe
        parallel_tools::initialize(2);
        int threads1 = parallel_tools::estimated_number_of_threads();
        parallel_tools::initialize(4);
        int threads2 = parallel_tools::estimated_number_of_threads();
        parallel_tools::initialize(0);
        int threads3 = parallel_tools::estimated_number_of_threads();
        EXPECT_GT(threads1, 0);
        EXPECT_GT(threads2, 0);
        EXPECT_GT(threads3, 0);
    }

    // ============================================================================
    // Consolidated Test 2: Basic Parallel For Execution
    // ============================================================================

    {
        // Test 1: Basic parallel_for with multiple backends
        const size_t                  size = 1000;
        std::vector<std::atomic<int>> data(size);
        simple_functor                func(data);

        // Initialize all to -1
        for (auto& elem : data)
        {
            elem.store(-1, std::memory_order_relaxed);
        }

        parallel_tools::local_scope(
            parallel_tools::config("std"),
            [&data, size, &func]() { parallel_tools::parallel_for(0, size, 100, func); });

        parallel_tools::local_scope(
            parallel_tools::config("tbb"),
            [&data, size, &func]() { parallel_tools::parallel_for(0, size, 100, func); });

        parallel_tools::local_scope(
            parallel_tools::config("openmp"),
            [&data, size, &func]() { parallel_tools::parallel_for(0, size, 100, func); });

        // Verify all elements were processed
        for (size_t i = 0; i < size; ++i)
        {
            EXPECT_EQ(data[i].load(std::memory_order_relaxed), static_cast<int>(i * 2))
                << "Element " << i << " was not processed correctly";
        }

        // Test 2: Const functor
        std::atomic<int> counter{0};
        const_functor    const_func(counter);
        parallel_tools::parallel_for(0, size, 100, const_func);
        EXPECT_EQ(counter.load(std::memory_order_relaxed), static_cast<int>(size));

        // Test 3: Functor with Initialize method
        std::vector<std::atomic<int>> init_data(size);
        std::atomic<int>              init_count{0};
        functor_with_initialize       init_func(init_data, init_count);
        parallel_tools::parallel_for(0, size, 100, init_func);

        for (size_t i = 0; i < size; ++i)
        {
            EXPECT_EQ(init_data[i].load(std::memory_order_relaxed), static_cast<int>(i));
        }
        EXPECT_GE(init_count.load(std::memory_order_relaxed), 1);
    }

    // ============================================================================
    // Consolidated Test 3: Parallel For Edge Cases
    // ============================================================================

    {
        std::vector<std::atomic<int>> data(100);
        simple_functor                func(data);

        // Test 1: Empty range (first == last)
        parallel_tools::parallel_for(5, 5, 10, func);
        SUCCEED();  // No crash expected

        // Test 2: Single element range
        data[5].store(-1, std::memory_order_relaxed);
        parallel_tools::parallel_for(5, 6, 10, func);
        EXPECT_EQ(data[5].load(std::memory_order_relaxed), 10);  // 5 * 2

        // Test 3: Grain size larger than range - should execute sequentially
        for (auto& elem : data)
        {
            elem.store(-1, std::memory_order_relaxed);
        }
        parallel_tools::parallel_for(0, 100, 1000, func);
        for (size_t i = 0; i < 100; ++i)
        {
            EXPECT_EQ(data[i].load(std::memory_order_relaxed), static_cast<int>(i * 2));
        }

        // Test 4: Reversed range (first > last) - should handle gracefully
        parallel_tools::parallel_for(10, 0, 5, func);
        SUCCEED();  // Should not crash

        // Test 5: Zero grain size - backend should handle appropriately
        for (auto& elem : data)
        {
            elem.store(-1, std::memory_order_relaxed);
        }
        parallel_tools::parallel_for(0, 100, 0, func);
        SUCCEED();  // Should handle gracefully
    }

    // ============================================================================
    // Consolidated Test 4: Parallel For with Various Grain Sizes and Large Ranges
    // ============================================================================

    {
        // Test 1: Various grain sizes
        const size_t                  size = 1000;
        std::vector<std::atomic<int>> data(size);
        std::vector<size_t>           grain_sizes = {1, 10, 50, 100, 500, 1000, 10000};

        for (size_t grain : grain_sizes)
        {
            // Reset data
            for (auto& elem : data)
            {
                elem.store(-1, std::memory_order_relaxed);
            }

            simple_functor func(data);
            parallel_tools::parallel_for(0, size, grain, func);

            // Verify
            for (size_t i = 0; i < size; ++i)
            {
                EXPECT_EQ(data[i].load(std::memory_order_relaxed), static_cast<int>(i * 2))
                    << "Failed with grain size " << grain << " at index " << i;
            }
        }

        // Test 2: Large range processing
        const size_t                  large_size = 100000;
        std::vector<std::atomic<int>> large_data(large_size);
        simple_functor                large_func(large_data);

        for (auto& elem : large_data)
        {
            elem.store(0, std::memory_order_relaxed);
        }

        parallel_tools::parallel_for(0, large_size, 1000, large_func);

        // Spot check some elements
        EXPECT_EQ(large_data[0].load(std::memory_order_relaxed), 0);
        EXPECT_EQ(large_data[1000].load(std::memory_order_relaxed), 2000);
        EXPECT_EQ(
            large_data[large_size - 1].load(std::memory_order_relaxed),
            static_cast<int>((large_size - 1) * 2));
    }

    // ============================================================================
    // Consolidated Test 5: Nested Parallelism and Parallel Scope Detection
    // ============================================================================

    {
        // Test 1: Set nested parallelism to false
        parallel_tools::set_nested_parallelism(false);
        bool nested = parallel_tools::nested_parallelism();
        EXPECT_FALSE(nested);

        // Test 2: Set nested parallelism to true (may or may not be supported)
        parallel_tools::set_nested_parallelism(true);
        SUCCEED();  // Just ensure no crash

        // Test 3: Toggle nested parallelism multiple times
        parallel_tools::set_nested_parallelism(false);
        EXPECT_FALSE(parallel_tools::nested_parallelism());
        parallel_tools::set_nested_parallelism(true);
        parallel_tools::set_nested_parallelism(false);
        EXPECT_FALSE(parallel_tools::nested_parallelism());

        // Test 4: Check parallel scope outside parallel region
        bool is_parallel = parallel_tools::is_parallel_scope();
        EXPECT_FALSE(is_parallel);

        // Test 5: Check parallel scope inside parallel region
        std::atomic<bool> inside_scope_result{false};
        auto              check_scope = [&inside_scope_result](size_t, size_t)
        {
            inside_scope_result.store(
                parallel_tools::is_parallel_scope(), std::memory_order_relaxed);
        };
        parallel_tools::parallel_for(0, 10, 5, check_scope);
        SUCCEED();  // Backend-dependent, just ensure no crash

        // Test 6: Single thread detection outside parallel region
        bool is_single = parallel_tools::single_thread();
        SUCCEED();  // Backend-specific behavior

        // Test 7: Single thread detection with one thread
        parallel_tools::initialize(1);
        is_single = parallel_tools::single_thread();
        SUCCEED();  // Backend-dependent
    }

    // ============================================================================
    // Consolidated Test 6: Configuration Structure
    // ============================================================================

    {
        // Test 1: Default configuration values
        parallel_tools::config cfg;
        EXPECT_EQ(cfg.max_number_of_threads_, 0);
        EXPECT_FALSE(cfg.nested_parallelism_);

        // Test 2: Configuration with thread count
        parallel_tools::config cfg_threads(4);
        EXPECT_EQ(cfg_threads.max_number_of_threads_, 4);

        // Test 3: Configuration with nested parallelism
        parallel_tools::config cfg_nested(true);
        EXPECT_TRUE(cfg_nested.nested_parallelism_);

        // Test 4: Verify THRESHOLD constant
        EXPECT_GT(parallel_tools::THRESHOLD, 0u);
        EXPECT_LE(parallel_tools::THRESHOLD, 1000000u);
    }

    // ============================================================================
    // Consolidated Test 7: Edge Cases and Error Handling
    // ============================================================================

    {
        // Test 1: Initialize with zero threads (should use default)
        parallel_tools::initialize(0);
        int num_threads = parallel_tools::estimated_number_of_threads();
        EXPECT_GT(num_threads, 0);

        // Test 2: Initialize with negative threads (should handle gracefully)
        parallel_tools::initialize(-1);
        num_threads = parallel_tools::estimated_number_of_threads();
        EXPECT_GT(num_threads, 0);

        // Test 3: Initialize with excessive thread count (backend should cap it)
        parallel_tools::initialize(10000);
        num_threads = parallel_tools::estimated_number_of_threads();
        EXPECT_GT(num_threads, 0);
        EXPECT_LT(num_threads, 10000);  // Should be capped
    }

    // ============================================================================
    // Consolidated Test 8: Thread Safety and Concurrency
    // ============================================================================

    {
        // Test 1: Parallel for thread safety with multiple executions
        const size_t                  size = 10000;
        std::vector<std::atomic<int>> data(size);

        for (auto& elem : data)
        {
            elem.store(0, std::memory_order_relaxed);
        }

        auto increment_func = [&data](size_t begin, size_t end)
        {
            for (size_t i = begin; i < end; ++i)
            {
                data[i].fetch_add(1, std::memory_order_relaxed);
            }
        };

        // Run multiple parallel_for calls
        parallel_tools::parallel_for(0, size, 100, increment_func);
        parallel_tools::parallel_for(0, size, 100, increment_func);
        parallel_tools::parallel_for(0, size, 100, increment_func);

        // Each element should be incremented exactly 3 times
        for (size_t i = 0; i < size; ++i)
        {
            EXPECT_EQ(data[i].load(std::memory_order_relaxed), 3)
                << "Thread safety violated at index " << i;
        }

        // Test 2: Concurrent parallel_for calls from different threads
        const size_t                  concurrent_size = 1000;
        std::vector<std::atomic<int>> data1(concurrent_size);
        std::vector<std::atomic<int>> data2(concurrent_size);

        for (auto& elem : data1)
            elem.store(0, std::memory_order_relaxed);
        for (auto& elem : data2)
            elem.store(0, std::memory_order_relaxed);

        std::thread t1(
            [&data1, concurrent_size]()
            {
                simple_functor func(data1);
                parallel_tools::parallel_for(0, concurrent_size, 100, func);
            });

        std::thread t2(
            [&data2, concurrent_size]()
            {
                simple_functor func(data2);
                parallel_tools::parallel_for(0, concurrent_size, 100, func);
            });

        t1.join();
        t2.join();

        // Verify both completed successfully
        for (size_t i = 0; i < concurrent_size; ++i)
        {
            EXPECT_EQ(data1[i].load(std::memory_order_relaxed), static_cast<int>(i * 2));
            EXPECT_EQ(data2[i].load(std::memory_order_relaxed), static_cast<int>(i * 2));
        }
    }

    // ============================================================================
    // Consolidated Test 9: Performance and Correctness with Large Datasets
    // ============================================================================

    {
        // Test 1: Correctness verification with large dataset and computation
        const size_t                  size = 100000;
        std::vector<int>              input(size);
        std::vector<std::atomic<int>> output(size);

        // Initialize input
        std::iota(input.begin(), input.end(), 0);

        // Process in parallel
        auto process_func = [&input, &output](size_t begin, size_t end)
        {
            for (size_t i = begin; i < end; ++i)
            {
                int result = input[i] * 3 + 7;
                output[i].store(result, std::memory_order_relaxed);
            }
        };

        parallel_tools::parallel_for(0, size, 1000, process_func);

        // Verify correctness
        for (size_t i = 0; i < size; ++i)
        {
            int expected = static_cast<int>(i) * 3 + 7;
            EXPECT_EQ(output[i].load(std::memory_order_relaxed), expected)
                << "Incorrect result at index " << i;
        }

        // Test 2: Data race detection with repeated operations
        const size_t                  race_size = 10000;
        std::vector<std::atomic<int>> counters(race_size);

        for (auto& c : counters)
        {
            c.store(0, std::memory_order_relaxed);
        }

        auto increment_counters = [&counters](size_t begin, size_t end)
        {
            for (size_t i = begin; i < end; ++i)
            {
                counters[i].fetch_add(1, std::memory_order_relaxed);
            }
        };

        // Run 10 times
        for (int iter = 0; iter < 10; ++iter)
        {
            parallel_tools::parallel_for(0, race_size, 100, increment_counters);
        }

        // Each counter should be exactly 10
        for (size_t i = 0; i < race_size; ++i)
        {
            EXPECT_EQ(counters[i].load(std::memory_order_relaxed), 10)
                << "Data race detected at index " << i;
        }
    }
}

}  // namespace parallel
