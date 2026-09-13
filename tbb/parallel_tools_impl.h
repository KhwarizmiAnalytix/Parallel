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

#pragma once

#include "common/parallel_export.h"
#include "common/parallel_tools_impl.h"

#ifdef _MSC_VER
#pragma push_macro("__TBB_NO_IMPLICIT_LINKAGE")
#define __TBB_NO_IMPLICIT_LINKAGE 1
#endif

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>

#ifdef _MSC_VER
#pragma pop_macro("__TBB_NO_IMPLICIT_LINKAGE")
#endif

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

void PARALLEL_API parallel_tools_impl_for_tbb(
    size_t                   first,
    size_t                   last,
    size_t                   grain,
    execute_functor_ptr_type functor_executer,
    void*                    functor);

//--------------------------------------------------------------------------------
template <typename T>
class func_call
{
    T& obj_;

    void operator=(const func_call&) = delete;

public:
    void operator()(const tbb::blocked_range<size_t>& r) const { obj_.Execute(r.begin(), r.end()); }

    func_call(T& obj) : obj_(obj) {}
};

//--------------------------------------------------------------------------------
template <typename FunctorInternal>
void execute_functor_tbb(void* functor, size_t first, size_t last, size_t grain)
{
    FunctorInternal& fi = *reinterpret_cast<FunctorInternal*>(functor);

    size_t range = last - first;
    if (range <= 0)
    {
        return;
    }
    if (grain > 0)
    {
        tbb::parallel_for(
            tbb::blocked_range<size_t>(first, last, grain), func_call<FunctorInternal>(fi));
    }
    else
    {
        // When the grain is not specified, automatically calculate an appropriate grain size so
        // most of the time will still be spent running the calculation and not task overhead.

        // Estimate of how many threads we might be able to run
        const size_t number_threads_estimate = 40;
        // Plan for a few batches per thread so one busy core doesn't stall the whole system
        const size_t batches_per_thread = 5;
        const size_t batches            = number_threads_estimate * batches_per_thread;

        if (range >= batches)
        {
            // std::ceil round up for systems without cmath
            size_t calculated_grain = ((range - 1) / batches) + 1;
            tbb::parallel_for(
                tbb::blocked_range<size_t>(first, last, calculated_grain),
                func_call<FunctorInternal>(fi));
        }
        else
        {
            // Data is too small to generate a reasonable grain. Fallback to default so data still runs
            // on as many threads as possible (Jan 2020: Default is one index per tbb task).
            tbb::parallel_for(
                tbb::blocked_range<size_t>(first, last), func_call<FunctorInternal>(fi));
        }
    }
}

//--------------------------------------------------------------------------------
template <>
template <typename FunctorInternal>
void parallel_tools_impl<backend_type::TBB>::parallel_for(
    size_t first, size_t last, size_t grain, FunctorInternal& fi)
{
    if (!nested_activated_ && is_parallel_)
    {
        fi.Execute(first, last);
    }
    else
    {
        // /!\ This behaviour should be changed if we want more control on nested
        // (e.g only the 2 first nested For are in parallel)
        bool from_parallel_code = is_parallel_.exchange(true);

        parallel_tools_impl_for_tbb(first, last, grain, execute_functor_tbb<FunctorInternal>, &fi);

        // Atomic contortion to achieve is_parallel_ &= from_parallel_code.
        // This compare&exchange basically boils down to:
        // if (is_parallel_ == true_flag)
        //   is_parallel_ = from_parallel_code;
        // else
        //   true_flag = is_parallel_;
        // Which either leaves is_parallel_ as false or sets it to from_parallel_code (i.e. &=).
        // Note that the return value of compare_exchange_weak() is not needed,
        // and that no looping is necessary.
        bool true_flag = true;
        is_parallel_.compare_exchange_weak(true_flag, from_parallel_code);
    }
}

//--------------------------------------------------------------------------------
template <>
PARALLEL_API void parallel_tools_impl<backend_type::TBB>::initialize(int);

//--------------------------------------------------------------------------------
template <>
PARALLEL_API parallel_tools_impl<backend_type::TBB>::parallel_tools_impl();

//--------------------------------------------------------------------------------
template <>
PARALLEL_API int parallel_tools_impl<backend_type::TBB>::estimated_default_number_of_threads();

//--------------------------------------------------------------------------------
template <>
PARALLEL_API int parallel_tools_impl<backend_type::TBB>::estimated_number_of_threads();

//--------------------------------------------------------------------------------
template <>
PARALLEL_API bool parallel_tools_impl<backend_type::TBB>::single_thread();

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
