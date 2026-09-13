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

#include <algorithm>  // For std::sort

#include "common/parallel_export.h"
#include "common/parallel_tools_impl.h"

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

int PARALLEL_API  number_of_threads_openmp();
bool PARALLEL_API single_thread_openmp();
void PARALLEL_API parallel_tools_impl_for_openmp(
    size_t                   first,
    size_t                   last,
    size_t                   grain,
    execute_functor_ptr_type functor_executer,
    void*                    functor,
    bool                     nested_activated);

//--------------------------------------------------------------------------------
template <typename FunctorInternal>
void execute_functor_openmp(void* functor, size_t from, size_t grain, size_t last)
{
    const size_t to = std::min(from + grain, last);

    FunctorInternal& fi = *reinterpret_cast<FunctorInternal*>(functor);
    fi.Execute(from, to);
}

//--------------------------------------------------------------------------------
template <>
template <typename FunctorInternal>
void parallel_tools_impl<backend_type::OpenMP>::parallel_for(
    size_t first, size_t last, size_t grain, FunctorInternal& fi)
{
    size_t n = last - first;
    if (n <= 0)
    {
        return;
    }

    if (grain >= n)
    {
        fi.Execute(first, last);
    }
    else
    {
        // /!\ This behaviour should be changed if we want more control on nested
        // (e.g only the 2 first nested For are in parallel)
        bool from_parallel_code = is_parallel_.exchange(true);

        parallel_tools_impl_for_openmp(
            first, last, grain, execute_functor_openmp<FunctorInternal>, &fi, nested_activated_);

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
PARALLEL_API void parallel_tools_impl<backend_type::OpenMP>::initialize(int);

//--------------------------------------------------------------------------------
template <>
PARALLEL_API int parallel_tools_impl<backend_type::OpenMP>::estimated_number_of_threads();

//--------------------------------------------------------------------------------
template <>
PARALLEL_API int parallel_tools_impl<backend_type::OpenMP>::estimated_default_number_of_threads();

//--------------------------------------------------------------------------------
template <>
PARALLEL_API bool parallel_tools_impl<backend_type::OpenMP>::single_thread();

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
