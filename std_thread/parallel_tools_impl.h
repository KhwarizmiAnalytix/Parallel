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

#include <algorithm>   // For std::sort
#include <functional>  // For std::bind

#include "common/parallel_export.h"
#include "common/parallel_tools_impl.h"
#include "std_thread/parallel_thread_pool.h"  // For parallel_thread_pool

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

int PARALLEL_API number_of_threads_stdthread();

//--------------------------------------------------------------------------------
template <>
template <typename FunctorInternal>
void parallel_tools_impl<backend_type::std_thread>::parallel_for(
    size_t first, size_t last, size_t grain, FunctorInternal& fi)
{
    size_t n = last - first;
    if (n <= 0)
    {
        return;
    }

    if (grain >= n || (!nested_activated_ && parallel_thread_pool::instance().is_parallel_scope()))
    {
        fi.Execute(first, last);
    }
    else
    {
        int thread_number = number_of_threads_stdthread();

        if (grain <= 0)
        {
            size_t estimate_grain =
                (last - first) / static_cast<size_t>(static_cast<long>(thread_number) * 4);
            grain = (estimate_grain > 0) ? estimate_grain : 1;
        }

        auto proxy =
            detail::parallel_impl::parallel_thread_pool::instance().allocate_threads(thread_number);

        for (size_t from = first; from < last; from += grain)
        {
            const auto to = (std::min)(from + grain, last);
            proxy.do_job([&fi, from, to] { fi.Execute(from, to); });
        }

        proxy.join();
    }
}

//--------------------------------------------------------------------------------
template <>
PARALLEL_API void parallel_tools_impl<backend_type::std_thread>::initialize(int);

//--------------------------------------------------------------------------------
template <>
PARALLEL_API int parallel_tools_impl<backend_type::std_thread>::estimated_number_of_threads();

//--------------------------------------------------------------------------------
template <>
PARALLEL_API int
parallel_tools_impl<backend_type::std_thread>::estimated_default_number_of_threads();

//--------------------------------------------------------------------------------
template <>
PARALLEL_API bool parallel_tools_impl<backend_type::std_thread>::single_thread();

//--------------------------------------------------------------------------------
template <>
PARALLEL_API bool parallel_tools_impl<backend_type::std_thread>::is_parallel_scope();

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
