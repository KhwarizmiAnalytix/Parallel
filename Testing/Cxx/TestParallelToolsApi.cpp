/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Comprehensive unit tests for parallel_tools_api (Internal API)
 *
 * Tests cover:
 * - Singleton pattern
 * - Backend identification and verification
 * - Thread initialization and configuration
 * - Nested parallelism management
 * - Parallel scope detection
 * - parallel_for integration with backend
 * - Thread safety
 * - Edge cases and error handling
 */

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "ParallelTest.h"
#include "common/parallel_tools_api.h"
#include "tools/parallel.h"

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

// ============================================================================
// Test Helper Classes
// ============================================================================

class test_functor_internal
{
public:
    std::vector<std::atomic<int>>& data_;

    test_functor_internal(std::vector<std::atomic<int>>& data) : data_(data) {}

    void Execute(size_t begin, size_t end)
    {
        for (size_t i = begin; i < end; ++i)
        {
            data_[i].store(static_cast<int>(i * 3), std::memory_order_relaxed);
        }
    }
};

// ============================================================================
// Test 1: Singleton and Backend Identification
// ============================================================================

PARALLELTEST(ParallelToolsApi, singleton_and_backend_identification)
{
    {
        parallel_tools_api& api1 = parallel_tools_api::instance();
        parallel_tools_api& api2 = parallel_tools_api::instance();
        EXPECT_EQ(&api1, &api2);

        backend_type type = parallel_tools_api::get_backend_type();
        EXPECT_TRUE(
            type == backend_type::std_thread || type == backend_type::TBB ||
            type == backend_type::OpenMP);

        const char* backend_name = parallel_tools_api::get_backend();
        ASSERT_NE(backend_name, nullptr);
        std::string name_str(backend_name);
        EXPECT_TRUE(name_str == "std" || name_str == "tbb" || name_str == "openmp");

        // Type and name must be consistent
        if (type == backend_type::std_thread)
            EXPECT_STREQ(backend_name, "std");
        else if (type == backend_type::TBB)
            EXPECT_STREQ(backend_name, "tbb");
        else if (type == backend_type::OpenMP)
            EXPECT_STREQ(backend_name, "openmp");
    }

    // ============================================================================
    // Test 2: Backend Setting Verification
    // ============================================================================

    {
        parallel_tools_api& api             = parallel_tools_api::instance();
        const char*         current_backend = parallel_tools_api::get_backend();

        EXPECT_TRUE(api.set_backend(current_backend));
        EXPECT_FALSE(api.set_backend("InvalidBackend"));
        EXPECT_FALSE(api.set_backend(nullptr));
    }

#if PARALLEL_HAS_OPENMP
    {
        parallel_tools_api& api = parallel_tools_api::instance();
        EXPECT_TRUE(api.set_backend("openmp"));
        EXPECT_FALSE(api.set_backend("tbb"));
        EXPECT_FALSE(api.set_backend("std"));
    }
#endif

#if PARALLEL_HAS_TBB
    {
        parallel_tools_api& api = parallel_tools_api::instance();
        EXPECT_TRUE(api.set_backend("tbb"));
        EXPECT_FALSE(api.set_backend("openmp"));
        EXPECT_FALSE(api.set_backend("std"));
    }
#endif

#if !PARALLEL_HAS_OPENMP && !PARALLEL_HAS_TBB
    {
        parallel_tools_api& api = parallel_tools_api::instance();
        EXPECT_TRUE(api.set_backend("std"));
        EXPECT_FALSE(api.set_backend("openmp"));
        EXPECT_FALSE(api.set_backend("tbb"));
    }
#endif

    // ============================================================================
    // Test 3: Thread Initialization and Configuration
    // Covers: auto default, single-thread floor, negative graceful, capped upper bound.
    // ============================================================================

    {
        parallel_tools_api& api = parallel_tools_api::instance();

        api.initialize(0);
        int num_threads = api.estimated_number_of_threads();
        EXPECT_GT(num_threads, 0);
        EXPECT_LE(num_threads, static_cast<int>(std::thread::hardware_concurrency() * 2));

        api.initialize(1);
        EXPECT_GE(api.estimated_number_of_threads(), 1);

        // Negative thread count must be handled gracefully
        api.initialize(-5);
        EXPECT_GT(api.estimated_number_of_threads(), 0);

        // Excessive request must be capped
        api.initialize(100000);
        num_threads = api.estimated_number_of_threads();
        EXPECT_GT(num_threads, 0);
        EXPECT_LT(num_threads, 100000);

        int default_threads = api.estimated_default_number_of_threads();
        EXPECT_GT(default_threads, 0);
        EXPECT_LE(default_threads, static_cast<int>(std::thread::hardware_concurrency() * 2));
    }

    // ============================================================================
    // Test 4: Nested Parallelism Management
    // ============================================================================

    {
        parallel_tools_api& api = parallel_tools_api::instance();

        api.set_nested_parallelism(false);
        EXPECT_FALSE(api.nested_parallelism());

        api.set_nested_parallelism(true);
        SUCCEED();

        // Toggle back and confirm state
        api.set_nested_parallelism(false);
        EXPECT_FALSE(api.nested_parallelism());
    }

    // ============================================================================
    // Test 5: Parallel Scope Detection
    // ============================================================================

    {
        parallel_tools_api& api = parallel_tools_api::instance();

        EXPECT_FALSE(api.is_parallel_scope());

        // single_thread() must not crash regardless of thread count
        api.initialize(0);
        SUCCEED();
        api.initialize(1);
        SUCCEED();

        EXPECT_FALSE(api.is_parallel_scope());
    }

    // ============================================================================
    // Test 6: Parallel For Execution
    // ============================================================================

    {
        parallel_tools_api& api = parallel_tools_api::instance();
        api.initialize(0);

        // Basic correctness: 10 000 elements, scope checked before and after
        {
            const size_t                  size = 10000;
            std::vector<std::atomic<int>> data(size);
            for (auto& elem : data)
                elem.store(0, std::memory_order_relaxed);

            EXPECT_FALSE(api.is_parallel_scope());
            test_functor_internal func(data);
            api.parallel_for(0, size, 1000, func);
            EXPECT_FALSE(api.is_parallel_scope());

            for (size_t i = 0; i < size; ++i)
            {
                EXPECT_EQ(data[i].load(std::memory_order_relaxed), static_cast<int>(i * 3))
                    << "Element " << i << " not processed correctly";
            }
        }

        // Large range: spot-check only to avoid O(n) assertion loop
        {
            const size_t                  size = 100000;
            std::vector<std::atomic<int>> data(size);
            for (auto& elem : data)
                elem.store(0, std::memory_order_relaxed);

            test_functor_internal func(data);
            api.parallel_for(0, size, 1000, func);

            EXPECT_EQ(data[0].load(std::memory_order_relaxed), 0);
            EXPECT_EQ(data[1000].load(std::memory_order_relaxed), 3000);
            EXPECT_EQ(
                data[size - 1].load(std::memory_order_relaxed), static_cast<int>((size - 1) * 3));
        }

        // Grain size coverage: small (< chunk), medium (≈ chunk), oversized (> range)
        {
            const size_t        size   = 1000;
            std::vector<size_t> grains = {1, 100, 5000};

            std::vector<std::atomic<int>> data(size);
            for (size_t grain : grains)
            {
                for (auto& elem : data)
                    elem.store(0, std::memory_order_relaxed);

                test_functor_internal func(data);
                api.parallel_for(0, size, grain, func);

                for (size_t i = 0; i < size; ++i)
                {
                    EXPECT_EQ(data[i].load(std::memory_order_relaxed), static_cast<int>(i * 3))
                        << "Failed with grain " << grain << " at index " << i;
                }
            }
        }

        // Edge cases: empty range, reversed range, zero grain
        {
            std::vector<std::atomic<int>> data(10);
            test_functor_internal         func(data);

            api.parallel_for(5, 5, 10, func);  // empty
            SUCCEED();
            api.parallel_for(10, 0, 5, func);  // reversed
            SUCCEED();
            for (auto& elem : data)
                elem.store(0, std::memory_order_relaxed);
            api.parallel_for(0, 10, 0, func);  // zero grain
            SUCCEED();
        }
    }

    // ============================================================================
    // Test 7: Thread Safety
    // ============================================================================

    {
        parallel_tools_api& api = parallel_tools_api::instance();

        // Concurrent parallel_for from two independent threads
        {
            std::vector<std::atomic<int>> data1(1000);
            std::vector<std::atomic<int>> data2(1000);

            for (auto& elem : data1)
                elem.store(0, std::memory_order_relaxed);
            for (auto& elem : data2)
                elem.store(0, std::memory_order_relaxed);

            std::thread t1(
                [&api, &data1]()
                {
                    test_functor_internal func(data1);
                    api.parallel_for(0, 1000, 100, func);
                });

            std::thread t2(
                [&api, &data2]()
                {
                    test_functor_internal func(data2);
                    api.parallel_for(0, 1000, 100, func);
                });

            t1.join();
            t2.join();

            for (size_t i = 0; i < 1000; ++i)
            {
                EXPECT_EQ(data1[i].load(std::memory_order_relaxed), static_cast<int>(i * 3));
                EXPECT_EQ(data2[i].load(std::memory_order_relaxed), static_cast<int>(i * 3));
            }
        }

        // Concurrent initialization must not crash
        {
            std::thread t1([&api]() { api.initialize(2); });
            std::thread t2([&api]() { api.initialize(4); });
            std::thread t3([&api]() { api.initialize(8); });

            t1.join();
            t2.join();
            t3.join();

            EXPECT_GT(api.estimated_number_of_threads(), 0);
        }
    }
}

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
