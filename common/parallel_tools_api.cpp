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

#include "common/parallel_tools_api.h"

#include <algorithm>  // For std::toupper
#include <cstdlib>    // For std::getenv
#include <iostream>   // For std::cerr
#include <string>     // For std::string

#include "tools/parallel.h"

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

//------------------------------------------------------------------------------
parallel_tools_api::parallel_tools_api()
{
    // Single backend instance is created as member variable (no dynamic allocation needed)
    // Set max thread number from env
    this->refresh_number_of_thread();
}

//------------------------------------------------------------------------------
// Thread-safe lazy initialization via C++11 magic statics (guaranteed)
// thread-safe by the standard, unlike a raw check-then-act pointer).
parallel_tools_api& parallel_tools_api::instance()
{
    static parallel_tools_api instance;
    return instance;
}

//------------------------------------------------------------------------------
backend_type parallel_tools_api::get_backend_type()
{
    return selected_backend_tools;
}

//------------------------------------------------------------------------------
const char* parallel_tools_api::get_backend()
{
    switch (selected_backend_tools)
    {
    case backend_type::std_thread:
        return "std";
    case backend_type::TBB:
        return "tbb";
    case backend_type::OpenMP:
        return "openmp";
    default:
        return nullptr;
    }
}

//------------------------------------------------------------------------------
bool parallel_tools_api::set_backend(const char* type)
{
    // Check for null pointer
    if (type == nullptr)
    {
        return false;
    }

    // Backend is selected at compile-time, so we just verify the requested backend
    // matches the compile-time selected one
    std::string backend(type);
    std::transform(backend.cbegin(), backend.cend(), backend.begin(), ::tolower);

    const char* current_backend = this->get_backend();
    std::string current_backend_upper(current_backend);
    std::transform(
        current_backend_upper.cbegin(),
        current_backend_upper.cend(),
        current_backend_upper.begin(),
        ::tolower);

    if (backend != current_backend_upper)
    {
        std::cerr << "WARNING: Backend selection is compile-time only. Requested \"" << type
                  << "\" but using \"" << current_backend << "\".\n";
        return false;
    }

    this->refresh_number_of_thread();
    return true;
}

//------------------------------------------------------------------------------
void parallel_tools_api::initialize(int num_threads)
{
    this->desired_number_of_thread_ = num_threads;
    this->refresh_number_of_thread();
}

//------------------------------------------------------------------------------
void parallel_tools_api::refresh_number_of_thread()
{
    const int num_threads = this->desired_number_of_thread_.load(std::memory_order_relaxed);
    backend_impl_.initialize(num_threads);
}

//------------------------------------------------------------------------------
int parallel_tools_api::estimated_default_number_of_threads()
{
    return backend_impl_.estimated_default_number_of_threads();
}

//------------------------------------------------------------------------------
int parallel_tools_api::estimated_number_of_threads()
{
    return backend_impl_.estimated_number_of_threads();
}

//------------------------------------------------------------------------------
void parallel_tools_api::set_nested_parallelism(bool is_nested)
{
    backend_impl_.set_nested_parallelism(is_nested);
}

//------------------------------------------------------------------------------
bool parallel_tools_api::nested_parallelism()
{
    return backend_impl_.nested_parallelism();
}

//------------------------------------------------------------------------------
bool parallel_tools_api::is_parallel_scope()
{
    return backend_impl_.is_parallel_scope();
}

//------------------------------------------------------------------------------
bool parallel_tools_api::single_thread()
{
    return backend_impl_.single_thread();
}

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
