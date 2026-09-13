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

#include <atomic>
#include <cstddef>

#include "common/parallel_export.h"
#include "tools/parallel.h"

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

template <backend_type Backend>
class parallel_tools_impl
{
public:
    //--------------------------------------------------------------------------------
    void initialize(int num_threads = 0);

    //--------------------------------------------------------------------------------
    int estimated_number_of_threads();

    //--------------------------------------------------------------------------------
    static int estimated_default_number_of_threads();

    //--------------------------------------------------------------------------------
    void set_nested_parallelism(bool is_nested);

    //--------------------------------------------------------------------------------
    bool nested_parallelism();

    //--------------------------------------------------------------------------------
    bool is_parallel_scope();

    //--------------------------------------------------------------------------------
    bool single_thread();

    //--------------------------------------------------------------------------------
    template <typename FunctorInternal>
    void parallel_for(size_t first, size_t last, size_t grain, FunctorInternal& fi);

    //--------------------------------------------------------------------------------
    parallel_tools_impl();

    //--------------------------------------------------------------------------------
    parallel_tools_impl(const parallel_tools_impl& other);

    //--------------------------------------------------------------------------------
    void operator=(const parallel_tools_impl& other);

private:
    bool              nested_activated_ = false;
    std::atomic<bool> is_parallel_{false};
};

template <backend_type Backend>
void parallel_tools_impl<Backend>::set_nested_parallelism(bool is_nested)
{
    nested_activated_ = is_nested;
}

template <backend_type Backend>
bool parallel_tools_impl<Backend>::nested_parallelism()
{
    return nested_activated_;
}

template <backend_type Backend>
bool parallel_tools_impl<Backend>::is_parallel_scope()
{
    return is_parallel_;
}

template <backend_type Backend>
parallel_tools_impl<Backend>::parallel_tools_impl() : nested_activated_(true), is_parallel_(false)
{
}

template <backend_type Backend>
parallel_tools_impl<Backend>::parallel_tools_impl(const parallel_tools_impl& other)
    : nested_activated_(other.nested_activated_), is_parallel_(other.is_parallel_.load())
{
}

template <backend_type Backend>
void parallel_tools_impl<Backend>::operator=(const parallel_tools_impl& other)
{
    nested_activated_ = other.nested_activated_;
    is_parallel_      = other.is_parallel_.load();
}

using execute_functor_ptr_type = void (*)(void*, size_t, size_t, size_t);

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
