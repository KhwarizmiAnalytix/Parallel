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

#include <charconv>
#include <cstdlib>  // For std::getenv()
#include <mutex>    // For std::mutex
#include <stack>    // For std::stack
#include <string>

#include "tbb/parallel_tools_impl.h"

#ifdef _MSC_VER
#pragma push_macro("__TBB_NO_IMPLICIT_LINKAGE")
#define __TBB_NO_IMPLICIT_LINKAGE 1  // NOLINT(bugprone-reserved-identifier)
#endif

#include <tbb/task_arena.h>  // For tbb:task_arena

#ifdef _MSC_VER
#pragma pop_macro("__TBB_NO_IMPLICIT_LINKAGE")
#endif

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

static int specified_num_threads_tbb;  // Default initialized to zero

// Process-wide state, each lazily initialized on first use via a
// function-local static (C++11 magic statics: guaranteed thread-safe,
// initialized on first call rather than at static-init time before
// main()). This replaces a Schwarz-counter static object whose
// constructor called std::make_unique (which can throw std::bad_alloc)
// during static initialization, where no exception handler could ever
// catch it.
static tbb::task_arena& get_task_arena()
{
    static tbb::task_arena arena;
    return arena;
}

static std::mutex& get_parallel_tools_mutex()
{
    static std::mutex mutex;
    return mutex;
}

static std::stack<int>& get_thread_id_stack()
{
    static std::stack<int> stack;
    return stack;
}

static std::mutex& get_thread_id_stack_mutex()
{
    static std::mutex mutex;
    return mutex;
}

//------------------------------------------------------------------------------
template <>
parallel_tools_impl<backend_type::TBB>::parallel_tools_impl() : nested_activated_(true)
{
}

//------------------------------------------------------------------------------
template <>
void parallel_tools_impl<backend_type::TBB>::initialize(int num_threads)
{
    get_parallel_tools_mutex().lock();

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
        else if (get_task_arena().is_active())
        {
            get_task_arena().terminate();
            specified_num_threads_tbb = 0;
        }
    }
    if (num_threads > 0 &&
        num_threads <=
            parallel_tools_impl<backend_type::TBB>::estimated_default_number_of_threads())
    {
        if (get_task_arena().is_active())
        {
            get_task_arena().terminate();
        }
        get_task_arena().initialize(num_threads);
        specified_num_threads_tbb = num_threads;
    }

    get_parallel_tools_mutex().unlock();
}

//------------------------------------------------------------------------------
template <>
int parallel_tools_impl<backend_type::TBB>::estimated_number_of_threads()
{
    return specified_num_threads_tbb > 0
               ? specified_num_threads_tbb
               : parallel_tools_impl<backend_type::TBB>::estimated_default_number_of_threads();
}

//------------------------------------------------------------------------------
template <>
int parallel_tools_impl<backend_type::TBB>::estimated_default_number_of_threads()
{
    return get_task_arena().max_concurrency();
}

//------------------------------------------------------------------------------
template <>
bool parallel_tools_impl<backend_type::TBB>::single_thread()
{
    // Check if we're inside a parallel region
    // If the stack is empty, we're not in a parallel region
    get_thread_id_stack_mutex().lock();
    const bool is_empty = get_thread_id_stack().empty();
    get_thread_id_stack_mutex().unlock();

    if (is_empty)
    {
        return false;
    }

    get_thread_id_stack_mutex().lock();
    const int top_id = get_thread_id_stack().top();
    get_thread_id_stack_mutex().unlock();

    return top_id == tbb::this_task_arena::current_thread_index();
}

//------------------------------------------------------------------------------
void parallel_tools_impl_for_tbb(
    size_t                   first,
    size_t                   last,
    size_t                   grain,
    execute_functor_ptr_type functor_executer,
    void*                    functor)
{
    get_thread_id_stack_mutex().lock();
    get_thread_id_stack().emplace(tbb::this_task_arena::current_thread_index());
    get_thread_id_stack_mutex().unlock();

    if (get_task_arena().is_active())
    {
        get_task_arena().execute([&] { functor_executer(functor, first, last, grain); });
    }
    else
    {
        functor_executer(functor, first, last, grain);
    }

    get_thread_id_stack_mutex().lock();
    get_thread_id_stack().pop();
    get_thread_id_stack_mutex().unlock();
}

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
