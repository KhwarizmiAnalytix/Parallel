/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Comprehensive test suite for threaded_task_queue functionality
 * Tests task queue operations, ordering, threading, and both return value and void specializations
 */

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "ParallelTest.h"
#include "tools/threaded_task_queue.h"

namespace parallel
{

// ============================================================================
// Test Group 1: Basic Functionality - Non-void Return Type
// ============================================================================

// Test 1: Simple task with int return type
PARALLELTEST(ThreadedTaskQueue, Test)
{
    {
        auto                          worker = [](int x) { return x * 2; };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        queue.push(5);

        int result = 0;
        EXPECT_TRUE(queue.pop(result));
        EXPECT_EQ(result, 10);
    }

    // Test 2: Multiple tasks with strict ordering
    {
        auto                          worker = [](int x) { return x * x; };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        const int num_tasks = 10;
        for (int i = 0; i < num_tasks; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        // Results should be in strict order
        for (int i = 0; i < num_tasks; ++i)
        {
            int result = 0;
            EXPECT_TRUE(queue.pop(result));
            EXPECT_EQ(result, i * i);
        }
    }

    // Test 3: Multiple arguments task
    {
        auto worker = [](int a, int b, int c) { return a + b + c; };
        threaded_task_queue<int, int, int, int> queue(worker, true, -1, 2);

        queue.push(1, 2, 3);
        queue.push(10, 20, 30);

        int result = 0;
        EXPECT_TRUE(queue.pop(result));
        EXPECT_EQ(result, 6);
        EXPECT_TRUE(queue.pop(result));
        EXPECT_EQ(result, 60);
    }

    // Test 4: try_pop when queue is empty
    {
        auto                          worker = [](int x) { return x; };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        int result = 0;
        EXPECT_FALSE(queue.try_pop(result));
    }

    // Test 5: try_pop when result is available
    {
        auto                          worker = [](int x) { return x * 3; };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        queue.push(7);

        // Give some time for task to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        int result = 0;
        EXPECT_TRUE(queue.try_pop(result));
        EXPECT_EQ(result, 21);
    }

    // Test 6: is_empty check
    {
        auto                          worker = [](int x) { return x; };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        EXPECT_TRUE(queue.is_empty());

        queue.push(1);
        EXPECT_FALSE(queue.is_empty());

        int result = 0;
        queue.pop(result);
        EXPECT_TRUE(queue.is_empty());
    }

    // Test 7: flush operation
    {
        auto worker = [](int x)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return x;
        };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        for (int i = 0; i < 10; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        queue.flush();
        EXPECT_TRUE(queue.is_empty());
    }

    // ============================================================================
    // Test Group 2: Buffer Size and Ordering
    // ============================================================================

    // Test 8: Buffer size limiting with non-strict ordering
    {
        auto worker = [](int x)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return x;
        };

        // Buffer size of 3, non-strict ordering
        threaded_task_queue<int, int> queue(worker, false, 3, 1);

        // Push many tasks quickly
        for (int i = 0; i < 10; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        // With buffer size 3 and 1 thread, older tasks should be dropped
        // Give time for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        int count  = 0;
        int result = 0;
        while (queue.try_pop(result))
        {
            count++;
        }

        // Should have fewer than 10 results due to buffer limit
        EXPECT_LT(count, 10);
    }

    // Test 9: Non-strict ordering allows skipping
    {
        std::atomic<int> counter{0};

        auto worker = [&counter](int x)
        {
            // Simulate variable execution time
            if (x == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            counter++;
            return x;
        };

        threaded_task_queue<int, int> queue(worker, false, -1, 4);

        queue.push(0);  // This will be slow
        queue.push(1);  // These might complete first
        queue.push(2);

        // With non-strict ordering, we might get results out of order
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        int  result     = 0;
        bool got_result = queue.try_pop(result);

        // We should be able to get at least one result
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        EXPECT_GE(counter.load(), 1);
    }

    // ============================================================================
    // Test Group 3: Void Return Type Specialization
    // ============================================================================

    // Test 10: Void task execution
    {
        std::atomic<int> counter{0};

        auto                           worker = [&counter](int x) { counter += x; };
        threaded_task_queue<void, int> queue(worker, true, -1, 2);

        queue.push(1);
        queue.push(2);
        queue.push(3);

        queue.flush();

        EXPECT_EQ(counter.load(), 6);
    }

    // Test 11: Void task with multiple arguments
    {
        std::atomic<int> sum{0};

        auto                                worker = [&sum](int a, int b) { sum += (a + b); };
        threaded_task_queue<void, int, int> queue(worker, true, -1, 2);

        queue.push(1, 2);
        queue.push(3, 4);
        queue.push(5, 6);

        queue.flush();

        EXPECT_EQ(sum.load(), 21);  // (1+2) + (3+4) + (5+6) = 21
    }

    // Test 12: Void task is_empty
    {
        std::atomic<int> counter{0};

        auto worker = [&counter](int x)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter += x;
        };
        threaded_task_queue<void, int> queue(worker, true, -1, 2);

        EXPECT_TRUE(queue.is_empty());

        queue.push(1);
        EXPECT_FALSE(queue.is_empty());

        queue.flush();
        EXPECT_TRUE(queue.is_empty());
    }

    // ============================================================================
    // Test Group 4: Threading and Concurrency
    // ============================================================================

    // Test 13: Concurrent task execution
    {
        std::atomic<int> active_count{0};
        std::atomic<int> max_concurrent{0};

        auto worker = [&active_count, &max_concurrent](int x)
        {
            int current  = active_count.fetch_add(1) + 1;
            int expected = max_concurrent.load();
            while (current > expected && !max_concurrent.compare_exchange_weak(expected, current))
            {
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            active_count.fetch_sub(1);
            return x;
        };

        threaded_task_queue<int, int> queue(worker, true, -1, 4);

        for (int i = 0; i < 10; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        queue.flush();

        // With 4 threads, we should see concurrent execution
        EXPECT_GE(max_concurrent.load(), 2);
    }

    // Test 14: Thread safety with shared state
    {
        std::atomic<int64_t> sum{0};

        auto worker = [&sum](int x)
        {
            sum.fetch_add(x, std::memory_order_relaxed);
            return x;
        };

        threaded_task_queue<int, int> queue(worker, true, -1, 4);

        const int num_tasks = 100;
        for (int i = 0; i < num_tasks; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        queue.flush();

        int64_t expected = (static_cast<int64_t>(num_tasks - 1) * num_tasks) / 2;
        EXPECT_EQ(sum.load(), expected);
    }

    // Test 15: Many threads
    {
        std::atomic<int> counter{0};

        auto worker = [&counter](int x)
        {
            counter++;
            return x;
        };

        threaded_task_queue<int, int> queue(worker, true, -1, 8);

        const int num_tasks = 100;
        for (int i = 0; i < num_tasks; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        queue.flush();

        EXPECT_EQ(counter.load(), num_tasks);
    }

    // ============================================================================
    // Test Group 5: Edge Cases
    // ============================================================================

    // Test 16: Single thread execution
    {
        auto                          worker = [](int x) { return x * 2; };
        threaded_task_queue<int, int> queue(worker, true, -1, 1);

        for (int i = 0; i < 10; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        // With strict ordering and single thread, results should be in order
        for (int i = 0; i < 10; ++i)
        {
            int result = 0;
            EXPECT_TRUE(queue.pop(result));
            EXPECT_EQ(result, i * 2);
        }
    }

    // Test 17: Pop on empty queue returns false
    {
        auto                          worker = [](int x) { return x; };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        int result = 0;
        EXPECT_FALSE(queue.pop(result));
    }

    // Test 18: Flush on empty queue
    {
        auto                          worker = [](int x) { return x; };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        // Should not hang or crash
        queue.flush();
        EXPECT_TRUE(queue.is_empty());
    }

    // Test 19: Task with complex return type
    {
        auto worker = [](int a, int b)
        {
            std::vector<int> result;
            for (int i = a; i <= b; ++i)
            {
                result.push_back(i);
            }
            return result;
        };

        threaded_task_queue<std::vector<int>, int, int> queue(worker, true, -1, 2);

        queue.push(1, 3);
        queue.push(5, 7);

        std::vector<int> result;
        EXPECT_TRUE(queue.pop(result));
        EXPECT_EQ(result.size(), 3);
        EXPECT_EQ(result[0], 1);
        EXPECT_EQ(result[1], 2);
        EXPECT_EQ(result[2], 3);

        EXPECT_TRUE(queue.pop(result));
        EXPECT_EQ(result.size(), 3);
        EXPECT_EQ(result[0], 5);
        EXPECT_EQ(result[1], 6);
        EXPECT_EQ(result[2], 7);
    }

    // Test 20: Stress test with many tasks
    {
        auto                          worker = [](int x) { return x; };
        threaded_task_queue<int, int> queue(worker, true, -1, 4);

        const int num_tasks = 1000;
        for (int i = 0; i < num_tasks; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        int result = 0;
        for (int i = 0; i < num_tasks; ++i)
        {
            EXPECT_TRUE(queue.pop(result));
            EXPECT_EQ(result, i);
        }

        EXPECT_TRUE(queue.is_empty());
    }

    // Test 21: Void task buffer size
    {
        std::atomic<int> counter{0};

        auto worker = [&counter](int x)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            counter += x;
        };

        threaded_task_queue<void, int> queue(worker, false, 3, 1);

        // Push many tasks quickly
        for (int i = 0; i < 10; ++i)
        {
            queue.push(1);
        }

        queue.flush();

        // With buffer size 3, some tasks should be dropped
        EXPECT_LT(counter.load(), 10);
    }

    // Test 22: Mixed speed tasks
    {
        auto worker = [](int x)
        {
            if (x % 2 == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return x * 2;
        };

        threaded_task_queue<int, int> queue(worker, true, -1, 4);

        for (int i = 0; i < 20; ++i)
        {
            int val = i;
            queue.push(std::move(val));
        }

        for (int i = 0; i < 20; ++i)
        {
            int result = 0;
            EXPECT_TRUE(queue.pop(result));
            EXPECT_EQ(result, i * 2);
        }
    }
}

}  // namespace parallel
