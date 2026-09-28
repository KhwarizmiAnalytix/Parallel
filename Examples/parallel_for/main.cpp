/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 *
 * Portions of this code are based on VTK (Visualization Toolkit):

 *   Licensed under BSD-3-Clause
 */

/**
 * Example: parallel_for and parallel_reduce
 *
 * Demonstrates using the Parallel library located via find_package(Parallel).
 * Build this example standalone (set CMAKE_PREFIX_PATH to the install prefix):
 *
 *   cmake -B build -DCMAKE_PREFIX_PATH=/path/to/install
 *   cmake --build build
 *   ./build/parallel_for_example
 */

#include "include/tools/parallel_tools.h"

#include <cstdlib>
#include <iostream>
#include <numeric>
#include <vector>

// Functor for parallel_for: squares each element over [begin, end).
struct square_functor
{
    explicit square_functor(std::vector<long>& data) : data_(data) {}

    void operator()(size_t begin, size_t end)
    {
        for (size_t i = begin; i < end; ++i)
        {
            data_[i] = static_cast<long>(i) * static_cast<long>(i);
        }
    }

    std::vector<long>& data_;
};

int main()
{
    // ---- Environment info ----------------------------------------------------
    std::cout << "backend : " << parallel_tools::estimated_number_of_threads()
              << " thread(s)\n\n";

    const size_t N     = 10000;
    int          fails = 0;

    // ---- parallel_for --------------------------------------------------------
    {
        std::vector<long> data(N, 0);
        square_functor    fn(data);

        parallel_tools::parallel_for(0, N, /*grain=*/512, fn);

        bool ok = true;
        for (size_t i = 0; i < N; ++i)
        {
            if (data[i] != static_cast<long>(i * i))
            {
                ok = false;
                break;
            }
        }
        std::cout << "parallel_for  [" << N << " squares]  : " << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok)
        {
            ++fails;
        }
    }

    // ---- parallel_reduce -----------------------------------------------------
    {
        std::vector<long> data(N);
        std::iota(data.begin(), data.end(), 0L);

        const long sum = parallel_tools::parallel_reduce(
            0,
            N,
            /*grain=*/512,
            0L,
            [&data](size_t first, size_t last, long init)
            {
                long partial = init;
                for (size_t i = first; i < last; ++i)
                {
                    partial += data[i];
                }
                return partial;
            },
            [](long a, long b) { return a + b; });

        const long expected = static_cast<long>(N - 1) * static_cast<long>(N) / 2;
        const bool ok       = (sum == expected);
        std::cout << "parallel_reduce [sum 0.." << N - 1 << "]: " << (ok ? "PASS" : "FAIL")
                  << "  (got " << sum << ", expected " << expected << ")\n";
        if (!ok)
        {
            ++fails;
        }
    }

    // ---- local_scope: switch backend at runtime ------------------------------
    {
        std::vector<long> data(N, -1);
        square_functor    fn(data);

        parallel_tools::local_scope(
            parallel_tools::config("std"),
            [&data, &fn]() { parallel_tools::parallel_for(0, N, 512, fn); });

        bool ok = true;
        for (size_t i = 0; i < N; ++i)
        {
            if (data[i] != static_cast<long>(i * i))
            {
                ok = false;
                break;
            }
        }
        std::cout << "local_scope(std) parallel_for     : " << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok)
        {
            ++fails;
        }
    }

    std::cout << "\n" << (fails == 0 ? "All checks PASSED." : "Some checks FAILED.") << "\n";
    return fails == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
