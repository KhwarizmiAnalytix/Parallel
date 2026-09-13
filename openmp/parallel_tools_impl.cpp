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

#include <omp.h>

#include <charconv>
#include <cstdlib>  // For std::getenv()
#include <stack>    // For std::stack
#include <string>

#include "openmp/parallel_tools_impl.h"

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

static int specified_num_threads_omp;  // Default initialized to zero

// Lazily initialized on first use via a function-local static (C++11 magic
// statics: guaranteed thread-safe, initialized on first call rather than at
// static-init time before main()). This replaces a Schwarz-counter static
// object whose constructor called std::make_unique (which can throw
// std::bad_alloc) during static initialization, where no exception handler
// could ever catch it.
static std::stack<int>& get_thread_id_stack()
{
    static std::stack<int> stack;
    return stack;
}

//------------------------------------------------------------------------------
template <>
void parallel_tools_impl<backend_type::OpenMP>::initialize(int num_threads)
{
    const int max_threads =
        parallel_tools_impl<backend_type::OpenMP>::estimated_default_number_of_threads();
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
        else if (specified_num_threads_omp != 0)
        {
            specified_num_threads_omp = 0;
            omp_set_num_threads(max_threads);
        }
    }
#pragma omp single
    if (num_threads > 0)
    {
        num_threads               = std::min(num_threads, max_threads);
        specified_num_threads_omp = num_threads;
        omp_set_num_threads(num_threads);
    }
}

//------------------------------------------------------------------------------
int number_of_threads_openmp()
{
    return (specified_num_threads_omp > 0)
               ? specified_num_threads_omp
               : parallel_tools_impl<backend_type::OpenMP>::estimated_default_number_of_threads();
}

//------------------------------------------------------------------------------
bool single_thread_openmp()
{
    // Guard against calls made outside of any parallel_for region, where the
    // stack is empty and top() would be undefined behavior (matches the TBB
    // backend's equivalent empty-stack guard).
    if (get_thread_id_stack().empty())
    {
        return false;
    }
    return get_thread_id_stack().top() == omp_get_thread_num();
}

//------------------------------------------------------------------------------
template <>
int parallel_tools_impl<backend_type::OpenMP>::estimated_number_of_threads()
{
    return number_of_threads_openmp();
}

//------------------------------------------------------------------------------
template <>
int parallel_tools_impl<backend_type::OpenMP>::estimated_default_number_of_threads()
{
    return omp_get_max_threads();
}

//------------------------------------------------------------------------------
template <>
bool parallel_tools_impl<backend_type::OpenMP>::single_thread()
{
    return single_thread_openmp();
}

//------------------------------------------------------------------------------
void parallel_tools_impl_for_openmp(
    size_t                   first,
    size_t                   last,
    size_t                   grain,
    execute_functor_ptr_type functor_executer,
    void*                    functor,
    bool                     nested_activated)
{
    if (grain == 0)
    {
        const size_t estimate_grain =
            (last - first) / (static_cast<size_t>(number_of_threads_openmp()) * 4);
        grain = (estimate_grain > 0) ? estimate_grain : 1;
    }

    omp_set_max_active_levels(static_cast<int>(nested_activated));

#pragma omp single
    get_thread_id_stack().emplace(omp_get_thread_num());

#pragma omp parallel for schedule(runtime)
    for (size_t from = first; from < last; from += grain)
    {
        functor_executer(functor, from, grain, last);
    }

#pragma omp single
    get_thread_id_stack().pop();
}

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
