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

#include "common/parallel_tools_impl.h"

#include <atomic>
#include <charconv>
#include <cstdlib>  // For std::getenv()
#include <string>
#include <thread>  // For std::thread::hardware_concurrency()

#include "std_thread/parallel_tools_impl.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace parallel
{
namespace detail
{
namespace parallel_impl
{
static std::atomic<int> specified_num_threads_std{0};

//------------------------------------------------------------------------------
int number_of_threads_stdthread()
{
    const int n = specified_num_threads_std.load(std::memory_order_relaxed);
    return (n != 0) ? static_cast<unsigned int>(n) : std::thread::hardware_concurrency();
}

//------------------------------------------------------------------------------
template <>
void parallel_tools_impl<backend_type::std_thread>::initialize(int num_threads)
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const int max_threads =
        parallel_tools_impl<backend_type::std_thread>::estimated_default_number_of_threads();
    if (num_threads == 0)
    {
        const char* parallel_num_threads = std::getenv("PARALLEL_MAX_THREADS");
        if (parallel_num_threads != nullptr)
        {
            std::string str(parallel_num_threads);
            auto        result = std::from_chars(str.data(), str.data() + str.size(), num_threads);
            if (result.ec != std::errc())
            {
                num_threads = 0;
            }
        }
        else
        {
            specified_num_threads_std = 0;
        }
    }
    if (num_threads > 0)
    {
        num_threads               = std::min(num_threads, max_threads);
        specified_num_threads_std = num_threads;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

//------------------------------------------------------------------------------
template <>
int parallel_tools_impl<backend_type::std_thread>::estimated_number_of_threads()
{
    const int n = specified_num_threads_std.load(std::memory_order_relaxed);
    return n > 0 ? n
                 : parallel_tools_impl<
                       backend_type::std_thread>::estimated_default_number_of_threads();
}

//------------------------------------------------------------------------------
template <>
int parallel_tools_impl<backend_type::std_thread>::estimated_default_number_of_threads()
{
#if defined(__EMSCRIPTEN_PTHREADS__) && (PARALLEL_WEBASSEMBLY_PARALLEL_THREAD_POOL_SIZE > 0)
    const int max_threads = PARALLEL_WEBASSEMBLY_PARALLEL_THREAD_POOL_SIZE;
#else
    const int max_threads = std::thread::hardware_concurrency();
#endif
    return max_threads;
}

//------------------------------------------------------------------------------
template <>
bool parallel_tools_impl<backend_type::std_thread>::single_thread()
{
    return parallel_thread_pool::instance().single_thread();
}

//------------------------------------------------------------------------------
template <>
bool parallel_tools_impl<backend_type::std_thread>::is_parallel_scope()
{
    return parallel_thread_pool::instance().is_parallel_scope();
}

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
