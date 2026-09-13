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
#include <memory>

#include "common/parallel_export.h"
#include "common/parallel_tools_impl.h"
#include "tools/parallel.h"

#if PARALLEL_HAS_TBB
#include "tbb/parallel_tools_impl.h"
#elif PARALLEL_HAS_OPENMP
#include "openmp/parallel_tools_impl.h"
#else
#include "std_thread/parallel_tools_impl.h"
#endif

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

// Compile-time backend selection based on availability
// Priority: TBB > OpenMP > std_thread
#if PARALLEL_HAS_TBB
constexpr backend_type selected_backend_tools = backend_type::TBB;
#elif PARALLEL_HAS_OPENMP
constexpr backend_type selected_backend_tools = backend_type::OpenMP;
#else
constexpr backend_type selected_backend_tools = backend_type::std_thread;
#endif

using selected_parallel_tools_impl = parallel_tools_impl<selected_backend_tools>;

class PARALLEL_VISIBILITY parallel_tools_api
{
public:
    //--------------------------------------------------------------------------------
    PARALLEL_API static parallel_tools_api& instance();

    //--------------------------------------------------------------------------------
    PARALLEL_API static backend_type get_backend_type();

    //--------------------------------------------------------------------------------
    PARALLEL_API static const char* get_backend();

    //--------------------------------------------------------------------------------
    PARALLEL_API bool set_backend(const char* type);

    //--------------------------------------------------------------------------------
    PARALLEL_API void initialize(int num_threads = 0);

    //--------------------------------------------------------------------------------
    PARALLEL_API int estimated_number_of_threads();

    //--------------------------------------------------------------------------------
    PARALLEL_API int estimated_default_number_of_threads();

    //------------------------------------------------------------------------------
    PARALLEL_API void set_nested_parallelism(bool is_nested);

    //--------------------------------------------------------------------------------
    PARALLEL_API bool nested_parallelism();

    //--------------------------------------------------------------------------------
    PARALLEL_API bool is_parallel_scope();

    //--------------------------------------------------------------------------------
    PARALLEL_API bool single_thread();

    //--------------------------------------------------------------------------------
    int get_internal_desired_number_of_thread() { return desired_number_of_thread_; }

    //------------------------------------------------------------------------------
    template <typename Config, typename T>
    void local_scope(Config const& config, T&& lambda)
    {
        const Config old_config(*this);
        *this << config;
        try
        {
            lambda();
        }
        catch (...)
        {
            *this << old_config;
            throw;
        }
        *this << old_config;
    }

    //--------------------------------------------------------------------------------
    // Direct backend execution using compile-time selected backend
    //--------------------------------------------------------------------------------
    template <typename FunctorInternal>
    void parallel_for(size_t first, size_t last, size_t grain, FunctorInternal& fi)
    {
        backend_impl_.parallel_for(first, last, grain, fi);
    }

    // disable copying
    parallel_tools_api(parallel_tools_api const&) = delete;
    void operator=(parallel_tools_api const&)     = delete;

private:
    //--------------------------------------------------------------------------------
    PARALLEL_API parallel_tools_api();

    //--------------------------------------------------------------------------------
    PARALLEL_API void refresh_number_of_thread();

    //--------------------------------------------------------------------------------
    template <typename Config>
    parallel_tools_api& operator<<(Config const& config)
    {
        this->initialize(config.max_number_of_threads_);
        this->set_backend(config.backend_.c_str());
        this->set_nested_parallelism(config.nested_parallelism_);
        return *this;
    }

    /**
   * Desired number of threads
   */
    std::atomic<int> desired_number_of_thread_{0};

    /**
   * Single backend implementation selected at compile-time
   */
    selected_parallel_tools_impl backend_impl_;
};

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
