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
 * @class   parallel_tools
 * @brief   A set of parallel (multi-threaded) utility functions.
 *
 * parallel_tools provides a set of utility functions that can
 * be used to parallelize parts of code using multiple threads.
 * There are several back-end implementations of parallel functionality
 * (currently std_thread, TBB, and OpenMP) that actual execution is
 * delegated to.
 */

#pragma once

#include <functional>   // For std::function
#include <string>       // For std::string
#include <type_traits>  // For std::enable_if
#include <utility>

#include "common/parallel_export.h"
#include "common/parallel_tools_api.h"

#if PARALLEL_HAS_TBB
#include <tbb/enumerable_thread_specific.h>
#endif

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

#if PARALLEL_HAS_OPENMP
// Static storage for OpenMP threadprivate
// Each thread gets its own copy automatically via OpenMP's threadprivate pragma.
// NOTE: Do NOT use 'thread_local' with OpenMP's threadprivate pragma - they are incompatible.
// OpenMP's threadprivate pragma provides thread-local storage for OpenMP parallel regions.
// `static` gives this internal linkage so each translation unit that includes this header
// gets its own distinct threadprivate object -- required for OpenMP's threadprivate pragma
// (each TU needs its own declaration) and avoids an ODR violation / duplicate-symbol link
// error when two TUs including this header end up statically linked into the same binary.
static unsigned char parallel_tools_functor_initialized = 0;
#pragma omp          threadprivate(parallel_tools_functor_initialized)
#endif

template <typename T>
class parallel_tools_has_initialize
{
    using no_type  = char (&)[1];
    using yes_type = char (&)[2];
    template <typename U, void (U::*)()>
    struct V
    {
    };
    template <typename U>
    static yes_type check(V<U, &U::Initialize>*);
    template <typename U>
    static no_type check(...);

public:
    static bool const value = sizeof(check<T>(nullptr)) == sizeof(yes_type);
};

template <typename T>
class parallel_tools_has_initialize_const
{
    using no_type  = char (&)[1];
    using yes_type = char (&)[2];
    template <typename U, void (U::*)() const>
    struct V
    {
    };
    template <typename U>
    static yes_type check(V<U, &U::Initialize>*);
    template <typename U>
    static no_type check(...);

public:
    static bool const value = sizeof(check<T>(0)) == sizeof(yes_type);
};

template <typename Functor, bool Init>
struct parallel_tools_functor_internal;

template <typename Functor>
struct parallel_tools_functor_internal<Functor, false>
{
    Functor& f_;
    parallel_tools_functor_internal(Functor& f) : f_(f) {}
    void Execute(size_t first, size_t last) { this->f_(first, last); }
    void parallel_for(size_t first, size_t last, size_t grain)
    {
        auto& SMPToolsAPI = parallel_tools_api::instance();
        SMPToolsAPI.parallel_for(first, last, grain, *this);
    }
    parallel_tools_functor_internal<Functor, false>& operator=(
        const parallel_tools_functor_internal<Functor, false>&);
    parallel_tools_functor_internal(const parallel_tools_functor_internal<Functor, false>&);
};

template <typename Functor>
struct parallel_tools_functor_internal<Functor, true>
{
    Functor& f_;

#if PARALLEL_HAS_TBB
    // TBB backend: Use enumerable_thread_specific
    mutable tbb::enumerable_thread_specific<unsigned char> initialized_;
#endif

    // cppcheck-suppress uninitMemberVar
    parallel_tools_functor_internal(Functor& f)
        : f_(f)
#if PARALLEL_HAS_TBB
          ,
          initialized_(0)
#endif
    {
    }

    void Execute(size_t first, size_t last)
    {
#if PARALLEL_HAS_OPENMP
        // OpenMP backend: Use threadprivate static
        if (!parallel_tools_functor_initialized)
        {
            this->f_.Initialize();
            parallel_tools_functor_initialized = 1;
        }
#elif PARALLEL_HAS_TBB
        // TBB backend: Use enumerable_thread_specific
        unsigned char& inited = initialized_.local();
        if (!inited)
        {
            this->f_.Initialize();
            inited = 1;
        }
#else
        // Native backend: Use standard C++ thread_local
        thread_local unsigned char initialized = 0;
        if (!initialized)
        {
            this->f_.Initialize();
            initialized = 1;
        }
#endif
        this->f_(first, last);
    }

    void parallel_for(size_t first, size_t last, size_t grain)
    {
        auto& SMPToolsAPI = parallel_tools_api::instance();
        SMPToolsAPI.parallel_for(first, last, grain, *this);
        this->f_.Reduce();
    }

    parallel_tools_functor_internal<Functor, true>& operator=(
        const parallel_tools_functor_internal<Functor, true>&);
    parallel_tools_functor_internal(const parallel_tools_functor_internal<Functor, true>&);
};

template <typename Functor>
class parallel_tools_lookup_for
{
    static bool const init = parallel_tools_has_initialize<Functor>::value;

public:
    using type = parallel_tools_functor_internal<Functor, init>;
};

template <typename Functor>
class parallel_tools_lookup_for<Functor const>
{
    static bool const init = parallel_tools_has_initialize_const<Functor>::value;

public:
    using type = parallel_tools_functor_internal<Functor const, init>;
};

template <typename T>
using resolved_not_int = typename std::enable_if<!std::is_integral<T>::value, void>::type;

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel

class PARALLEL_VISIBILITY parallel_tools
{
public:
    ///@{
    /**
   * @brief Execute a for operation in parallel using a functor (legacy API).
   *
   * This API is maintained for backward compatibility with existing code.
   * For new code, prefer the lambda-based overloads above.
   *
   * The functor should implement:
   * - operator()(size_t first, size_t last)
   * - Optional: Initialize() method for per-thread initialization
   * - Optional: Reduce() method for result aggregation
   *
   * @param first The start of the range (inclusive)
   * @param last The end of the range (exclusive)
   * @param grain Hint about coarseness for parallelization
   * @param f Functor object
   */
    template <typename Functor>
    static void parallel_for(size_t first, size_t last, size_t grain, Functor& f)
    {
        typename parallel::detail::parallel_impl::parallel_tools_lookup_for<Functor>::type fi(f);
        fi.parallel_for(first, last, grain);
    }

    template <typename Functor>
    static void parallel_for(size_t first, size_t last, size_t grain, const Functor& f)
    {
        typename parallel::detail::parallel_impl::parallel_tools_lookup_for<Functor const>::type fi(
            f);
        fi.parallel_for(first, last, grain);
    }

    /**
   * /!\ This method is not thread safe.
   * Initialize the underlying libraries for execution.
   */
    PARALLEL_API static void initialize(int num_threads = 0);

    /**
   * Get the estimated number of threads being used by the backend.
   */
    PARALLEL_API static int estimated_number_of_threads();

    /**
   * Get the estimated number of threads being used by the backend by default.
   */
    PARALLEL_API static int estimated_default_number_of_threads();

    /**
   * /!\ This method is not thread safe.
   * If true enable nested parallelism for underlying backends.
   */
    PARALLEL_API static void set_nested_parallelism(bool is_nested);

    /**
   * Get true if the nested parallelism is enabled.
   */
    PARALLEL_API static bool nested_parallelism();

    /**
   * Return true if it is called from a parallel scope.
   */
    PARALLEL_API static bool is_parallel_scope();

    /**
   * Returns true if the given thread is specified thread
   * for single scope. Returns false otherwise.
   */
    PARALLEL_API static bool single_thread();

    /**
   * Structure used to specify configuration for local_scope() method.
   */
    struct config
    {
        int         max_number_of_threads_ = 0;
        std::string backend_               = "std";
        bool        nested_parallelism_    = false;

        config() = default;
        config(int max_num_threads) : max_number_of_threads_(max_num_threads) {}
        config(bool nested) : nested_parallelism_(nested) {}
        config(std::string backend) : backend_(std::move(backend)) {}
        config(int max_num_threads, std::string backend, bool nested)
            : max_number_of_threads_(max_num_threads),
              backend_(std::move(backend)),
              nested_parallelism_(nested)
        {
        }
        config(parallel::detail::parallel_impl::parallel_tools_api& API)
            : max_number_of_threads_(API.get_internal_desired_number_of_thread()),
              backend_(API.get_backend()),
              nested_parallelism_(API.nested_parallelism())
        {
        }
    };

    /**
   * Threshold used by various cases to switch between serial and threaded execution.
   */
    static constexpr size_t THRESHOLD = 100000;

    /**
   * /!\ This method is not thread safe.
   * Change the number of threads locally within this scope and call a functor.
   */
    template <typename T>
    static void local_scope(config const& cfg, T&& lambda)
    {
        auto& SMPToolsAPI = parallel::detail::parallel_impl::parallel_tools_api::instance();
        SMPToolsAPI.local_scope<parallel_tools::config>(cfg, lambda);
    }
};
