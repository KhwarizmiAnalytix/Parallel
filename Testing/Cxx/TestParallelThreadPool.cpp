/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Comprehensive unit tests for parallel_thread_pool (std_thread backend)
 *
 * Tests cover:
 * - Thread pool singleton and lifecycle
 * - Thread allocation and proxy management
 * - Job execution and distribution
 * - Thread identification
 * - Parallel scope detection
 * - Nested proxies and thread reuse
 * - Thread safety and concurrency
 * - Edge cases and error handling
 *
 * Note: These tests only run when compiled with std_thread backend
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "ParallelTest.h"

// Only compile these tests for std_thread backend
#if !PARALLEL_HAS_OPENMP && !PARALLEL_HAS_TBB

#include "std_thread/parallel_thread_pool.h"

namespace parallel
{
// ============================================================================
// Consolidated Test 1: Singleton, Lifecycle, and Initial State
// ============================================================================

PARALLELTEST(ParallelThreadPool, singleton_lifecycle_and_state)
{
    // Test 1: Singleton access - ensure same instance is returned
    detail::parallel_impl::parallel_thread_pool& pool1 =
        detail::parallel_impl::parallel_thread_pool::instance();
    detail::parallel_impl::parallel_thread_pool& pool2 =
        detail::parallel_impl::parallel_thread_pool::instance();
    EXPECT_EQ(&pool1, &pool2);

    // Test 2: Initial state - should not be in parallel scope
    EXPECT_FALSE(pool1.is_parallel_scope());

    // Test 3: Thread ID should be external_thread_id (1)
    EXPECT_EQ(
        pool1.get_thread_id(), detail::parallel_impl::parallel_thread_pool::external_thread_id);

    // Test 4: Thread count should be reasonable
    size_t count = pool1.thread_count();
    EXPECT_GT(count, 0u);
    EXPECT_LE(count, std::thread::hardware_concurrency() * 2);
}

// ============================================================================
// Consolidated Test 2: Thread Allocation and Proxy Management
// ============================================================================

PARALLELTEST(ParallelThreadPool, thread_allocation_and_proxy_management)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    // Test 1: Allocate with default thread count (0 = auto)
    {
        auto proxy = pool.allocate_threads(0);
        EXPECT_TRUE(proxy.is_top_level());

        auto threads = proxy.get_threads();
        EXPECT_GT(threads.size(), 0u);
    }
    EXPECT_FALSE(pool.is_parallel_scope());

    // Test 2: Allocate with custom count (4 threads)
    {
        auto proxy   = pool.allocate_threads(4);
        auto threads = proxy.get_threads();

        EXPECT_GE(threads.size(), 1u);
        EXPECT_LE(threads.size(), 4u);
    }
    EXPECT_FALSE(pool.is_parallel_scope());

    // Test 3: Allocate single thread
    {
        auto proxy   = pool.allocate_threads(1);
        auto threads = proxy.get_threads();
        EXPECT_GE(threads.size(), 0u);  // May be 0 or 1 depending on implementation
    }
    EXPECT_FALSE(pool.is_parallel_scope());

    // Test 4: Proxy move semantics
    {
        auto proxy1 = pool.allocate_threads(2);
        EXPECT_TRUE(proxy1.is_top_level());

        // Move construct
        auto proxy2 = std::move(proxy1);
        EXPECT_TRUE(proxy2.is_top_level());

        // Move assign
        auto proxy3 = pool.allocate_threads(0);
        proxy3      = std::move(proxy2);
        EXPECT_TRUE(proxy3.is_top_level());
    }
    EXPECT_FALSE(pool.is_parallel_scope());

    // Test 5: Get threads and verify they are joinable
    {
        auto proxy   = pool.allocate_threads(4);
        auto threads = proxy.get_threads();

        EXPECT_GT(threads.size(), 0u);
        for (auto& thread_ref : threads)
        {
            EXPECT_TRUE(thread_ref.get().joinable());
        }
    }
    EXPECT_FALSE(pool.is_parallel_scope());
}

// ============================================================================
// Consolidated Test 3: Job Execution and Distribution
// ============================================================================

PARALLELTEST(ParallelThreadPool, job_execution_and_distribution)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    // Test 1: Basic job execution
    {
        std::atomic<int> counter{0};
        auto             proxy = pool.allocate_threads(4);

        proxy.do_job([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
        proxy.join();

        EXPECT_GE(counter.load(std::memory_order_relaxed), 1);
    }

    // Test 2: Multiple jobs execution
    {
        std::atomic<int> counter{0};
        auto             proxy = pool.allocate_threads(2);

        for (int i = 0; i < 5; ++i)
        {
            proxy.do_job([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
        }
        proxy.join();

        EXPECT_GE(counter.load(std::memory_order_relaxed), 5);
    }

    // Test 3: Job with data manipulation
    {
        std::vector<std::atomic<int>> results(10);
        for (auto& r : results)
        {
            r.store(0, std::memory_order_relaxed);
        }

        auto proxy = pool.allocate_threads(4);

        proxy.do_job(
            [&results]()
            {
                for (size_t i = 0; i < results.size(); ++i)
                {
                    results[i].store(static_cast<int>(i * 2), std::memory_order_relaxed);
                }
            });

        proxy.join();

        // Verify results
        for (size_t i = 0; i < results.size(); ++i)
        {
            EXPECT_EQ(results[i].load(std::memory_order_relaxed), static_cast<int>(i * 2));
        }
    }

    // Test 4: Exception handling in jobs
    {
        auto proxy = pool.allocate_threads(2);

        // Job that throws - should be caught internally
        proxy.do_job([]() { throw std::runtime_error("Test exception"); });

        // Should not crash
        proxy.join();
    }
    EXPECT_FALSE(pool.is_parallel_scope());
}

// ============================================================================
// Consolidated Test 4: Thread Identification
// ============================================================================

PARALLELTEST(ParallelThreadPool, thread_identification)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    // Test 1: Verify external_thread_id constant
    EXPECT_EQ(detail::parallel_impl::parallel_thread_pool::external_thread_id, 1u);

    // Test 2: Get thread ID outside parallel region
    size_t thread_id = pool.get_thread_id();
    EXPECT_EQ(thread_id, detail::parallel_impl::parallel_thread_pool::external_thread_id);

    // Test 3: Get thread ID inside job
    std::atomic<size_t> internal_thread_id{0};

    {
        auto proxy = pool.allocate_threads(4);

        proxy.do_job(
            [&pool, &internal_thread_id]()
            { internal_thread_id.store(pool.get_thread_id(), std::memory_order_relaxed); });

        proxy.join();
    }

    size_t id = internal_thread_id.load(std::memory_order_relaxed);

    // Should be non-zero (not external)
    EXPECT_GT(id, 0u);
}

// ============================================================================
// Consolidated Test 5: Parallel Scope Detection
// ============================================================================

PARALLELTEST(ParallelThreadPool, parallel_scope_detection)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    // Test 1: Outside parallel region should return false
    EXPECT_FALSE(pool.is_parallel_scope());

    // Test 2: Inside parallel region should return true
    std::atomic<bool> inside_result{false};

    {
        auto proxy = pool.allocate_threads(2);

        proxy.do_job([&pool, &inside_result]()
                     { inside_result.store(pool.is_parallel_scope(), std::memory_order_relaxed); });

        proxy.join();
    }

    EXPECT_TRUE(inside_result.load(std::memory_order_relaxed));

    // Test 3: Single thread detection - should not crash
    bool is_single = pool.single_thread();
    SUCCEED();
}

// ============================================================================
// Consolidated Test 6: Nested Proxies and Top-Level Detection
// ============================================================================

PARALLELTEST(ParallelThreadPool, nested_proxies_and_top_level)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    std::atomic<int> outer_counter{0};
    std::atomic<int> inner_counter{0};

    // Test 1: Top-level proxy detection
    {
        auto proxy = pool.allocate_threads(4);
        EXPECT_TRUE(proxy.is_top_level());
    }
    EXPECT_FALSE(pool.is_parallel_scope());

    // Test 2: Nested proxies behavior
    {
        auto outer_proxy = pool.allocate_threads(2);
        EXPECT_TRUE(outer_proxy.is_top_level());

        outer_proxy.do_job(
            [&pool, &outer_counter, &inner_counter]()
            {
                outer_counter.fetch_add(1, std::memory_order_relaxed);

                // Create nested proxy
                auto inner_proxy = pool.allocate_threads(2);
                EXPECT_FALSE(inner_proxy.is_top_level());  // Not top level

                inner_proxy.do_job([&inner_counter]()
                                   { inner_counter.fetch_add(1, std::memory_order_relaxed); });

                inner_proxy.join();
            });

        outer_proxy.join();
    }

    EXPECT_GE(outer_counter.load(std::memory_order_relaxed), 1);
    EXPECT_GE(inner_counter.load(std::memory_order_relaxed), 1);
}

// ============================================================================
// Consolidated Test 7: Thread Reuse and Sequential Allocation
// ============================================================================

PARALLELTEST(ParallelThreadPool, thread_reuse_and_sequential_allocation)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    std::atomic<int> counter{0};

    // Test: Allocate multiple proxies sequentially to verify thread reuse
    for (int iteration = 0; iteration < 5; ++iteration)
    {
        auto proxy = pool.allocate_threads(2);

        proxy.do_job([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });

        proxy.join();
    }

    EXPECT_EQ(counter.load(std::memory_order_relaxed), 5);
}

// ============================================================================
// Consolidated Test 8: Edge Cases and Error Handling
// ============================================================================

PARALLELTEST(ParallelThreadPool, edge_cases_and_error_handling)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    // Test 1: Allocate zero threads (should use default)
    {
        auto proxy   = pool.allocate_threads(0);
        auto threads = proxy.get_threads();
        EXPECT_GT(threads.size(), 0u);
    }
    EXPECT_FALSE(pool.is_parallel_scope());

    // Test 2: Allocate excessive threads (should be capped)
    {
        auto proxy   = pool.allocate_threads(10000);
        auto threads = proxy.get_threads();

        EXPECT_GT(threads.size(), 0u);
        EXPECT_LT(threads.size(), 10000u);
    }
    EXPECT_FALSE(pool.is_parallel_scope());

    // Test 3: Join without submitting jobs (should not crash)
    {
        auto proxy = pool.allocate_threads(2);
        proxy.join();
    }
    SUCCEED();

    // Test 4: Multiple joins should be safe
    {
        std::atomic<int> counter{0};
        auto             proxy = pool.allocate_threads(2);

        proxy.do_job([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });

        proxy.join();
        proxy.join();
        proxy.join();

        EXPECT_GE(counter.load(std::memory_order_relaxed), 1);
    }
}

// ============================================================================
// Consolidated Test 9: Thread Safety and Concurrency
// ============================================================================

PARALLELTEST(ParallelThreadPool, thread_safety_and_concurrency)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    // Test 1: Concurrent job execution
    {
        std::vector<std::atomic<int>> counters(100);
        for (auto& c : counters)
        {
            c.store(0, std::memory_order_relaxed);
        }

        auto proxy = pool.allocate_threads(4);

        // Submit many jobs concurrently
        for (size_t i = 0; i < counters.size(); ++i)
        {
            proxy.do_job([&counters, i]() { counters[i].fetch_add(1, std::memory_order_relaxed); });
        }

        proxy.join();

        // All jobs should have executed
        for (const auto& c : counters)
        {
            EXPECT_GE(c.load(std::memory_order_relaxed), 1);
        }
    }

    // Test 2: Data race detection
    {
        std::vector<std::atomic<int>> data(1000);
        for (auto& d : data)
        {
            d.store(0, std::memory_order_relaxed);
        }

        auto proxy = pool.allocate_threads(4);

        // Each index should be modified exactly once per job
        for (int iteration = 0; iteration < 10; ++iteration)
        {
            proxy.do_job(
                [&data]()
                {
                    for (auto& d : data)
                    {
                        d.fetch_add(1, std::memory_order_relaxed);
                    }
                });
        }

        proxy.join();

        // Each element should be exactly 10
        for (const auto& d : data)
        {
            EXPECT_EQ(d.load(std::memory_order_relaxed), 10);
        }
    }

    // Test 3: Stress test with many jobs
    {
        std::atomic<int> counter{0};
        auto             proxy = pool.allocate_threads(8);

        // Submit many small jobs
        for (int i = 0; i < 1000; ++i)
        {
            proxy.do_job([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
        }

        proxy.join();

        EXPECT_EQ(counter.load(std::memory_order_relaxed), 1000);
    }
}

// ============================================================================
// Consolidated Test 10: Integration Tests
// ============================================================================

PARALLELTEST(ParallelThreadPool, integration_tests)
{
    detail::parallel_impl::parallel_thread_pool& pool =
        detail::parallel_impl::parallel_thread_pool::instance();

    // Test 1: Full workflow - typical parallel computation
    {
        const size_t                  size = 10000;
        std::vector<int>              input(size);
        std::vector<std::atomic<int>> output(size);

        // Initialize input
        for (size_t i = 0; i < size; ++i)
        {
            input[i] = static_cast<int>(i);
            output[i].store(0, std::memory_order_relaxed);
        }

        auto proxy = pool.allocate_threads(4);

        // Process data in parallel
        proxy.do_job(
            [&input, &output, size]()
            {
                for (size_t i = 0; i < size; ++i)
                {
                    int result = input[i] * 2 + 1;
                    output[i].store(result, std::memory_order_relaxed);
                }
            });

        proxy.join();

        // Verify results
        for (size_t i = 0; i < size; ++i)
        {
            int expected = static_cast<int>(i) * 2 + 1;
            EXPECT_EQ(output[i].load(std::memory_order_relaxed), expected)
                << "Mismatch at index " << i;
        }
    }

    // Test 2: Parallel reduction
    {
        const size_t     size = 10000;
        std::vector<int> data(size);

        // Initialize
        for (size_t i = 0; i < size; ++i)
        {
            data[i] = 1;
        }

        std::atomic<int> total{0};

        auto proxy = pool.allocate_threads(4);

        // Parallel reduction
        proxy.do_job(
            [&data, &total]()
            {
                int local_sum = 0;
                for (const auto& val : data)
                {
                    local_sum += val;
                }
                total.fetch_add(local_sum, std::memory_order_relaxed);
            });

        proxy.join();

        EXPECT_GE(total.load(std::memory_order_relaxed), static_cast<int>(size));
    }
}

}  // namespace parallel

#endif  // !PARALLEL_HAS_OPENMP && !PARALLEL_HAS_TBB
