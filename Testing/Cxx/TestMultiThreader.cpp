/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Comprehensive test suite for multi_threader functionality
 * Tests thread creation, execution, synchronization, and thread management
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "ParallelTest.h"
#include "tools/multi_threader.h"

namespace parallel
{

// ============================================================================
// Helper Functions (must be declared outside test body)
// ============================================================================

// Helper function for single method tests
static void single_method_worker(void* data)
{
    auto* ti      = static_cast<multi_threader::thread_info*>(data);
    auto* counter = static_cast<std::atomic<int>*>(ti->user_data);
    counter->fetch_add(1, std::memory_order_relaxed);
}

// Helper function that uses thread info
static void thread_info_worker(void* data)
{
    auto* ti       = static_cast<multi_threader::thread_info*>(data);
    auto* counters = static_cast<std::atomic<int>*>(ti->user_data);

    EXPECT_GE(ti->thread_id, 0);
    EXPECT_LT(ti->thread_id, ti->number_of_threads);

    counters[ti->thread_id].fetch_add(1, std::memory_order_relaxed);
}

// Helper functions for multiple method tests
static void method_0(void* data)
{
    auto* ti       = static_cast<multi_threader::thread_info*>(data);
    auto* counters = static_cast<std::atomic<int>*>(ti->user_data);
    counters[0].fetch_add(1, std::memory_order_relaxed);
}

static void method_1(void* data)
{
    auto* ti       = static_cast<multi_threader::thread_info*>(data);
    auto* counters = static_cast<std::atomic<int>*>(ti->user_data);
    counters[1].fetch_add(1, std::memory_order_relaxed);
}

static void method_2(void* data)
{
    auto* ti       = static_cast<multi_threader::thread_info*>(data);
    auto* counters = static_cast<std::atomic<int>*>(ti->user_data);
    counters[2].fetch_add(1, std::memory_order_relaxed);
}

static void method_3(void* data)
{
    auto* ti       = static_cast<multi_threader::thread_info*>(data);
    auto* counters = static_cast<std::atomic<int>*>(ti->user_data);
    counters[3].fetch_add(1, std::memory_order_relaxed);
}

// Helper for spawn thread tests
static void spawned_worker(void* data)
{
    auto* ti      = static_cast<multi_threader::thread_info*>(data);
    auto* counter = static_cast<std::atomic<int>*>(ti->user_data);

    // Check if thread should terminate (active_flag becomes 0)
    while (ti->active_flag && ti->active_flag->load(std::memory_order_acquire) != 0)
    {
        counter->fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ============================================================================
// Test Group 1: Basic Functionality
// ============================================================================

// Comprehensive test for MultiThreader
PARALLELTEST(MultiThreader, Test)
{
    // Test 1: Create and destroy multi_threader
    {
        multi_threader* mt = multi_threader::create();
        EXPECT_TRUE(mt != nullptr);
        delete mt;
    }

    // Test 2: Get default number of threads
    {
        int default_threads = multi_threader::get_global_default_number_of_threads();
        EXPECT_GT(default_threads, 0);
    }

    // Test 3: Get maximum number of threads
    {
        int max_threads = multi_threader::get_global_static_maximum_number_of_threads();
        EXPECT_EQ(max_threads, PARALLEL_MAX_THREADS);
    }

    // Test 4: Set and get number of threads
    {
        multi_threader* mt = multi_threader::create();

        mt->set_number_of_threads(4);
        EXPECT_EQ(mt->get_number_of_threads(), 4);

        mt->set_number_of_threads(8);
        EXPECT_EQ(mt->get_number_of_threads(), 8);

        delete mt;
    }

    // Test 5: Clamping to valid range
    {
        multi_threader* mt = multi_threader::create();

        // Test minimum clamping
        mt->set_number_of_threads(0);
        EXPECT_GE(mt->get_number_of_threads(), 1);

        // Test maximum clamping
        mt->set_number_of_threads(PARALLEL_MAX_THREADS + 10);
        EXPECT_LE(mt->get_number_of_threads(), PARALLEL_MAX_THREADS);

        delete mt;
    }

    // ============================================================================
    // Test Group 2: Single Method Execution
    // ============================================================================

    // Test 6: Single method with multiple threads
    {
        multi_threader* mt = multi_threader::create();
        mt->set_number_of_threads(4);

        std::atomic<int> counter{0};
        mt->set_single_method(single_method_worker, &counter);
        mt->single_method_execute();

        EXPECT_EQ(counter.load(), 4);
        delete mt;
    }

    // Test 7: Thread info validation
    {
        multi_threader* mt = multi_threader::create();
        mt->set_number_of_threads(4);

        std::atomic<int> counters[4];
        for (int i = 0; i < 4; ++i)
        {
            counters[i].store(0);
        }

        mt->set_single_method(thread_info_worker, counters);
        mt->single_method_execute();

        // Each thread should have executed once
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(counters[i].load(), 1);
        }

        delete mt;
    }

    // Test 8: Single thread execution
    {
        multi_threader* mt = multi_threader::create();
        mt->set_number_of_threads(1);

        std::atomic<int> counter{0};
        mt->set_single_method(single_method_worker, &counter);
        mt->single_method_execute();

        EXPECT_EQ(counter.load(), 1);
        delete mt;
    }

    // ============================================================================
    // Test Group 3: Multiple Method Execution
    // ============================================================================

    // Test 9: Multiple different methods
    {
        multi_threader* mt = multi_threader::create();
        mt->set_number_of_threads(4);

        std::atomic<int> counters[4];
        for (int i = 0; i < 4; ++i)
        {
            counters[i].store(0);
        }

        mt->set_multiple_method(0, method_0, counters);
        mt->set_multiple_method(1, method_1, counters);
        mt->set_multiple_method(2, method_2, counters);
        mt->set_multiple_method(3, method_3, counters);

        mt->multiple_method_execute();

        // Each method should have been called once
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(counters[i].load(), 1);
        }

        delete mt;
    }

    // ============================================================================
    // Test Group 4: Spawn Thread
    // ============================================================================

    // Test 10: Spawn and terminate thread
    {
        multi_threader* mt = multi_threader::create();

        std::atomic<int> counter{0};
        int              thread_id = mt->spawn_thread(spawned_worker, &counter);

        EXPECT_GE(thread_id, 0);
        EXPECT_LT(thread_id, PARALLEL_MAX_THREADS);

        // Wait a bit for thread to start
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Thread should be active
        EXPECT_TRUE(mt->is_thread_active(thread_id));

        // Terminate the thread
        mt->terminate_thread(thread_id);

        // Give it time to terminate
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        delete mt;
    }

    // Test 11: Multiple spawned threads
    {
        multi_threader* mt = multi_threader::create();

        std::atomic<int> counters[3];
        for (int i = 0; i < 3; ++i)
        {
            counters[i].store(0);
        }

        int thread_ids[3];
        thread_ids[0] = mt->spawn_thread(spawned_worker, &counters[0]);
        thread_ids[1] = mt->spawn_thread(spawned_worker, &counters[1]);
        thread_ids[2] = mt->spawn_thread(spawned_worker, &counters[2]);

        // All should have different IDs
        EXPECT_NE(thread_ids[0], thread_ids[1]);
        EXPECT_NE(thread_ids[1], thread_ids[2]);
        EXPECT_NE(thread_ids[0], thread_ids[2]);

        // Wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // All should be active
        EXPECT_TRUE(mt->is_thread_active(thread_ids[0]));
        EXPECT_TRUE(mt->is_thread_active(thread_ids[1]));
        EXPECT_TRUE(mt->is_thread_active(thread_ids[2]));

        // Terminate all
        mt->terminate_thread(thread_ids[0]);
        mt->terminate_thread(thread_ids[1]);
        mt->terminate_thread(thread_ids[2]);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        delete mt;
    }

    // ============================================================================
    // Test Group 5: Thread Identification
    // ============================================================================

    // Test 12: Get current thread ID
    {
        multi_threader_id_type id1 = multi_threader::get_current_thread_id();
        multi_threader_id_type id2 = multi_threader::get_current_thread_id();

        // Same thread should have same ID
        EXPECT_TRUE(multi_threader::threads_equal(id1, id2));
    }

    // Test 13: Different threads have different IDs
    {
        std::atomic<bool>      thread_started{false};
        multi_threader_id_type main_id = multi_threader::get_current_thread_id();
        multi_threader_id_type spawned_id;

        auto worker = [&]()
        {
            spawned_id = multi_threader::get_current_thread_id();
            thread_started.store(true);
        };

        std::thread t(worker);

        // Wait for thread to start
        while (!thread_started.load())
        {
            std::this_thread::yield();
        }

        // Different threads should have different IDs
        EXPECT_FALSE(multi_threader::threads_equal(main_id, spawned_id));

        t.join();
    }

    // ============================================================================
    // Test Group 6: Global Settings
    // ============================================================================

    // Test 14: Set and get global maximum
    {
        int original = multi_threader::get_global_maximum_number_of_threads();

        multi_threader::set_global_maximum_number_of_threads(8);
        EXPECT_EQ(multi_threader::get_global_maximum_number_of_threads(), 8);

        // Restore original
        multi_threader::set_global_maximum_number_of_threads(original);
    }

    // Test 15: Set and get global default
    {
        int original = multi_threader::get_global_default_number_of_threads();

        multi_threader::set_global_default_number_of_threads(4);
        EXPECT_EQ(multi_threader::get_global_default_number_of_threads(), 4);

        // Restore original
        multi_threader::set_global_default_number_of_threads(original);
    }

    // ============================================================================
    // Test Group 7: Stress Tests
    // ============================================================================

    // Test 16: Many single method executions
    {
        multi_threader* mt = multi_threader::create();
        mt->set_number_of_threads(4);

        std::atomic<int> counter{0};
        mt->set_single_method(single_method_worker, &counter);

        for (int i = 0; i < 100; ++i)
        {
            mt->single_method_execute();
        }

        EXPECT_EQ(counter.load(), 400);  // 100 executions * 4 threads
        delete mt;
    }

    // Test 17: Rapid thread number changes
    {
        multi_threader* mt = multi_threader::create();

        for (int i = 1; i <= 8; ++i)
        {
            mt->set_number_of_threads(i);
            EXPECT_EQ(mt->get_number_of_threads(), i);
        }

        delete mt;
    }

    // Test 18: Maximum threads execution
    {
        multi_threader* mt = multi_threader::create();
        mt->set_number_of_threads(PARALLEL_MAX_THREADS);

        std::atomic<int> counter{0};
        mt->set_single_method(single_method_worker, &counter);
        mt->single_method_execute();

        EXPECT_EQ(counter.load(), PARALLEL_MAX_THREADS);
        delete mt;
    }
}
}  // namespace parallel
