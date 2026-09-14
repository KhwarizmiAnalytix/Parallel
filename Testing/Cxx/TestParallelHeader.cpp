/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Compile-check and smoke test for the "Parallel/parallel.h" umbrella header:
 * verifies that including it alone (no individual per-class header includes)
 * exposes every public class documented in the README's Public API section.
 */

#include <atomic>
#include <vector>

#include "Parallel/parallel.h"
#include "ParallelTest.h"

namespace parallel
{

PARALLELTEST(ParallelHeader, exposes_full_public_api)
{
    // parallel_tools: parallel_for + parallel_reduce
    {
        parallel_tools::initialize(0);

        std::vector<int> data(100, 1);
        parallel_tools::parallel_for(
            0,
            data.size(),
            10,
            [&data](size_t first, size_t last)
            {
                for (size_t i = first; i < last; ++i)
                {
                    data[i] = 2;
                }
            });
        EXPECT_EQ(data[50], 2);

        int sum = parallel_tools::parallel_reduce(
            0,
            data.size(),
            10,
            0,
            [&data](size_t first, size_t last, int init)
            {
                int partial = init;
                for (size_t i = first; i < last; ++i)
                {
                    partial += data[i];
                }
                return partial;
            },
            [](int a, int b) { return a + b; });
        EXPECT_EQ(sum, static_cast<int>(data.size()) * 2);
    }

    // multi_threader
    {
        multi_threader* mt = multi_threader::create();
        mt->set_number_of_threads(2);

        std::atomic<int> counter{0};
        mt->set_single_method(
            [](void* data)
            {
                auto* info = static_cast<multi_threader::thread_info*>(data);
                static_cast<std::atomic<int>*>(info->user_data)->fetch_add(1);
            },
            &counter);
        mt->single_method_execute();

        EXPECT_EQ(counter.load(), 2);
        delete mt;
    }

    // threaded_callback_queue
    {
        threaded_callback_queue queue;
        queue.set_number_of_threads(2);

        auto future = queue.push([](int x) { return x * x; }, 6);
        EXPECT_EQ(queue.get(future), 36);
    }

    // threaded_task_queue
    {
        auto                          worker = [](int x) { return x + 1; };
        threaded_task_queue<int, int> queue(worker, true, -1, 2);

        queue.push(41);
        int result = 0;
        EXPECT_TRUE(queue.pop(result));
        EXPECT_EQ(result, 42);
    }
}

}  // namespace parallel
