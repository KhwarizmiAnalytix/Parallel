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

#include "include/std_thread/parallel_thread_pool.h"

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <future>
#include <iostream>
#include <limits>
#include <memory>

#include "include/common/parallel_tools_impl.h"

namespace parallel
{
namespace detail
{
namespace parallel_impl
{

/**
 * @brief Sentinel value indicating no job is currently running on a thread
 */
static constexpr std::size_t no_running_job = std::numeric_limits<std::size_t>::max();

/**
 * @brief Represents a single job/task to be executed by a thread in the pool
 *
 * Each job encapsulates:
 * - The proxy that submitted it (for tracking ownership)
 * - The function to execute
 * - A promise to signal completion
 */
struct parallel_thread_pool::thread_job
{
    thread_job(proxy_data* proxy = nullptr, std::function<void()> function = nullptr)
        : proxy_{proxy}, function_{std::move(function)}
    {
    }

    proxy_data*           proxy_{};   ///< Proxy that owns this job
    std::function<void()> function_;  ///< The actual work to be performed
    std::promise<void>    promise_;   ///< Promise to signal job completion
};

/**
 * @brief Encapsulates all data for a single physical thread in the pool
 *
 * Each thread maintains:
 * - A queue of jobs to execute
 * - Index of currently running job (if any)
 * - The underlying std::thread
 * - Synchronization primitives
 */
struct parallel_thread_pool::thread_data
{
    std::vector<thread_job> jobs_;                         ///< Queue of pending jobs
    std::size_t             running_job_{no_running_job};  ///< Index of active job
    std::thread             systethread_;                  ///< The actual OS thread
    std::mutex              mutex_;                        ///< Protects job queue and state
    std::condition_variable condition_variable_;           ///< For wait/notify operations
};

/**
 * @brief Associates a thread_data with a virtual thread ID for a proxy
 *
 * Used to track which physical threads are allocated to which proxy,
 * and provide logical thread IDs for parallel region identification.
 */
struct parallel_thread_pool::proxy_thread_data
{
    proxy_thread_data(thread_data* thread_data = nullptr, std::size_t id = 0)
        : thread_{thread_data}, id_{id}
    {
    }

    thread_data* thread_{};  ///< Pointer to physical thread data
    std::size_t  id_{};      ///< Virtual thread ID for this proxy
};

/**
 * @brief Internal data for a proxy object
 *
 * Contains all state needed for a proxy to manage its allocated threads
 * and submitted jobs. Supports nested proxies (local scopes) via parent pointer.
 */
struct parallel_thread_pool::proxy_data
{
    parallel_thread_pool*          pool_{};         ///< Owning thread pool
    proxy_data*                    parent_{};       ///< Parent proxy (for nested scopes)
    std::vector<proxy_thread_data> threads_;        ///< Allocated physical threads
    std::size_t                    next_thread_{};  ///< Round-robin index for job distribution
    std::vector<std::future<void>> jobs_futures_;   ///< Futures for submitted jobs
    std::mutex                     mutex_;          ///< Protects proxy state
};

/**
 * @brief Executes a job on the calling thread
 *
 * This is the core job execution routine. It:
 * 1. Marks the job as running
 * 2. Releases the lock to allow concurrent operations
 * 3. Executes the job function (with exception handling)
 * 4. Signals completion via the job's promise
 * 5. Removes the job from the queue
 *
 * @param data The thread_data containing the job queue
 * @param job_index Index of the job to execute
 * @param lock Unique lock that must be held on entry (will be released during execution)
 *
 * @note The lock must be held on entry and will be held on exit
 * @note Exceptions from job functions are caught and logged, not propagated
 */
void parallel_thread_pool::run_job(
    thread_data& data, std::size_t job_index, std::unique_lock<std::mutex>& lock)
{
    assert(lock.owns_lock() && "Caller must have locked mutex");
    assert(job_index < data.jobs_.size() && "job_index out of range");

    // Save the old running job index (for nested job support)
    const auto old_running_job = data.running_job_;
    data.running_job_          = job_index;
    auto function              = std::move(data.jobs_[data.running_job_].function_);

    // Release lock during job execution to allow other threads to proceed
    lock.unlock();

    // Execute the job with exception safety
    try
    {
        function();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Function called by " << parallel_thread_pool::instance().get_thread_id()
                  << " has thrown an exception. The exception is ignored. what():\n"
                  << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Function called by " << parallel_thread_pool::instance().get_thread_id()
                  << " has thrown an unknown exception. The exception is ignored." << std::endl;
    }

    // Reacquire lock to clean up job state
    lock.lock();
    data.jobs_[data.running_job_].promise_.set_value();  // Signal completion
    data.jobs_.erase(data.jobs_.begin() + job_index);    // Remove completed job
    data.running_job_ = old_running_job;                 // Restore previous state
}

/**
 * @brief Private constructor for proxy - only callable by parallel_thread_pool
 */
parallel_thread_pool::proxy::proxy(std::unique_ptr<proxy_data>&& data) : data_{std::move(data)} {}

/**
 * @brief Destructor ensures all jobs are completed before destruction
 *
 *
 * If the proxy is destroyed with pending jobs (i.e., join() was not called),
 * the program is terminated to prevent undefined behavior.
 */
parallel_thread_pool::proxy::~proxy()
{
    if (data_ != nullptr && !data_->jobs_futures_.empty())
    {
        std::cerr << "Proxy not joined. Terminating." << std::endl;
        std::terminate();
    }
}

parallel_thread_pool::proxy::proxy(proxy&&) noexcept                                  = default;
parallel_thread_pool::proxy& parallel_thread_pool::proxy::operator=(proxy&&) noexcept = default;

/**
 * @brief Wait for all submitted jobs to complete
 *
 * Behavior depends on whether this is a top-level or nested proxy:
 *
 * Top-level proxy (called from outside thread pool):
 * - Simply waits on all job futures to complete
 *
 * Nested proxy (called from within thread pool):
 * - Actively helps execute jobs from the current thread's queue
 * - This prevents deadlocks when nested parallel regions occur
 * - Only processes jobs belonging to this proxy
 * - After local jobs are done, waits for jobs on other threads
 *
 * @note Must be called before proxy destruction
 * @note For nested proxies, must be called from the same thread that created the proxy
 */
void parallel_thread_pool::proxy::join()
{
    if (this->is_top_level())
    {
        // Top-level proxy: just wait for all futures
        for (auto& future : data_->jobs_futures_)
        {
            future.wait();
        }
    }
    else
    {
        // Nested proxy: help execute jobs to prevent deadlock
        thread_data& thread_data_ref = *data_->threads_[0].thread_;
        assert(thread_data_ref.systethread_.get_id() == std::this_thread::get_id());

        // Execute all jobs belonging to this proxy from the current thread's queue
        while (true)
        {
            std::unique_lock<std::mutex> lock{thread_data_ref.mutex_};

            // Find next job owned by this proxy
            auto it = std::find_if(
                thread_data_ref.jobs_.begin(),
                thread_data_ref.jobs_.end(),
                [this](const thread_job& job) { return job.proxy_ == data_.get(); });

            if (it == thread_data_ref.jobs_.end())
            {
                break;  // No more jobs for this proxy
            }

            const auto job_index =
                static_cast<std::size_t>(std::distance(thread_data_ref.jobs_.begin(), it));
            run_job(thread_data_ref, job_index, lock);
        }

        // Wait for jobs on other threads to complete
        for (auto& future : data_->jobs_futures_)
        {
            future.wait();
        }
    }

    data_->jobs_futures_.clear();
}

/**
 * @brief Submit a job to be executed by the thread pool
 *
 * Uses round-robin scheduling to distribute jobs across allocated threads.
 *
 * Special handling for nested proxies:
 * - If the job is assigned to thread 0 (the calling thread), it's added
 *   to the queue without notification (will be executed during join())
 * - This prevents the current thread from waiting on itself
 *
 * For other threads:
 * - Job is added to the thread's queue
 * - A future is stored to track completion
 * - Thread is notified to wake up and process the job
 *
 * @param job The function to execute
 *
 * @note Jobs are distributed in round-robin fashion across allocated threads
 * @note Must call join() to ensure all jobs complete
 */
void parallel_thread_pool::proxy::do_job(std::function<void()> job)
{
    // Round-robin thread selection
    data_->next_thread_ = (data_->next_thread_ + 1) % data_->threads_.size();
    auto& proxy_thread  = data_->threads_[data_->next_thread_];

    // Special case: nested proxy submitting to its own thread (thread 0)
    if (!this->is_top_level() && data_->next_thread_ == 0)
    {
        assert(std::this_thread::get_id() == proxy_thread.thread_->systethread_.get_id());

        // Add job to queue without notification (will be executed in join())
        const std::unique_lock<std::mutex> lock{proxy_thread.thread_->mutex_};
        proxy_thread.thread_->jobs_.emplace_back(data_.get(), std::move(job));
    }
    else
    {
        // Normal case: submit to another thread
        std::unique_lock<std::mutex> lock{proxy_thread.thread_->mutex_};

        auto& jobs = proxy_thread.thread_->jobs_;
        jobs.emplace_back(data_.get(), std::move(job));
        data_->jobs_futures_.emplace_back(jobs.back().promise_.get_future());

        lock.unlock();

        // Wake up the target thread to process the job
        proxy_thread.thread_->condition_variable_.notify_one();
    }
}

/**
 * @brief Get references to all system threads used by this proxy
 *
 * @return Vector of std::thread references
 */
std::vector<std::reference_wrapper<std::thread>> parallel_thread_pool::proxy::get_threads()
    const  // NOLINT(readability-convert-member-functions-to-static)
{
    std::vector<std::reference_wrapper<std::thread>> output;

    for (auto& proxy_thread : data_->threads_)
    {
        output.emplace_back(proxy_thread.thread_->systethread_);
    }

    return output;
}

/**
 * @brief Check if this proxy was created from outside the thread pool
 *
 * @return true if this is a top-level proxy (no parent), false if nested
 */
bool parallel_thread_pool::proxy::is_top_level() const noexcept
{
    return data_->parent_ == nullptr;
}

/**
 * @brief Constructor creates the thread pool with default number of threads
 *
 * The pool size is determined by the backend implementation's default
 * (typically hardware_concurrency()). All threads are created immediately
 * and remain alive until the pool is destroyed.
 *
 * Initialization sequence:
 * 1. Query backend for default thread count
 * 2. Create thread_data structures for each thread
 * 3. Spawn worker threads (they wait for initialized_ flag)
 * 4. Set initialized_ flag to release worker threads
 */
parallel_thread_pool::parallel_thread_pool()
{
    const auto thread_count = static_cast<std::size_t>(
        parallel_tools_impl<backend_type::std_thread>::estimated_default_number_of_threads());
    threads_.reserve(thread_count);
    for (std::size_t i{}; i < thread_count; ++i)
    {
        auto data          = std::make_unique<thread_data>();
        data->systethread_ = this->make_thread();
        threads_.emplace_back(std::move(data));
    }

    // Release worker threads to start processing
    initialized_.store(true, std::memory_order_release);
}

/**
 * @brief Destructor signals threads to exit and waits for them to complete
 *
 * Shutdown sequence:
 * 1. Set joining_ flag to signal threads to exit after finishing current jobs
 * 2. Notify all threads to wake up and check the flag
 * 3. Join all threads (wait for them to complete)
 *
 * @note Any pending jobs will be completed before threads exit
 * @note Blocking call - waits for all threads to finish
 */
parallel_thread_pool::~parallel_thread_pool()
{
    joining_.store(true, std::memory_order_release);

    // Wake every worker: use notify_all per CV (not notify_one). On some Windows/MinGW
    // runtimes a single notify_one can be missed relative to the predicate wait in
    // make_thread(), leaving workers stuck in condition_variable::wait until process
    // teardown — which surfaces as CTest hitting TIMEOUT on CoreCxxTests while gtest
    // has already finished.
    for (const auto& thread_data_ptr : threads_)
    {
        thread_data_ptr->condition_variable_.notify_all();
    }

    // Wait for all threads to finish
    for (const auto& thread_data_ptr : threads_)
    {
        thread_data_ptr->systethread_.join();
    }
}

/**
 * @brief Allocate a proxy to submit work to the thread pool
 *
 * Creates a proxy with the requested number of threads. Behavior differs
 * based on whether this is called from within the pool (nested) or from
 * outside (top-level):
 *
 * Top-level allocation (called from outside pool):
 * - Allocates the first N threads from the pool sequentially
 * - Returns a proxy with no parent
 *
 * Nested allocation (called from within a parallel region):
 * - First thread is always the calling thread (to avoid deadlock)
 * - Remaining threads are allocated from threads not used by parent proxies
 * - Returns a proxy with parent pointer set (for hierarchy tracking)
 *
 * @param thread_count Number of threads to allocate (0 = use all available)
 * @return A proxy object for submitting jobs
 *
 * @note The proxy must be joined before destruction
 * @note Requesting more threads than available returns all threads
 */
parallel_thread_pool::proxy parallel_thread_pool::allocate_threads(std::size_t thread_count)
{
    if (thread_count == 0 || thread_count > this->thread_count())
    {
        thread_count = this->thread_count();
    }

    auto proxy   = std::make_unique<proxy_data>();
    proxy->pool_ = this;
    proxy->threads_.reserve(thread_count);

    thread_data* thread_data_ptr = this->get_caller_thread_data();
    if (thread_data_ptr != nullptr)
    {
        // Nested proxy: allocate from within a parallel region
        proxy->parent_ = thread_data_ptr->jobs_[thread_data_ptr->running_job_].proxy_;
        proxy->threads_.emplace_back(thread_data_ptr, this->get_next_thread_id());
        this->fill_threads_for_nested_proxy(proxy.get(), thread_count);
    }
    else
    {
        // Top-level proxy: allocate from outside the pool
        proxy->parent_ = nullptr;
        for (std::size_t i{}; i < thread_count; ++i)
        {
            proxy->threads_.emplace_back(threads_[i].get(), this->get_next_thread_id());
        }
    }

    return parallel_thread_pool::proxy{std::move(proxy)};
}

/**
 * @brief Get the virtual thread ID for the calling thread
 *
 * Returns the logical thread ID within the current parallel region.
 * This ID is assigned by the proxy and is different from the physical
 * thread ID or std::thread::id.
 *
 * @return Virtual thread ID (starting from 1), or external_thread_id
 *         if called from outside the pool
 *
 * @note Thread IDs are unique within a proxy's lifetime
 * @note Returns external_thread_id (1) for non-pool threads
 */
std::size_t parallel_thread_pool::get_thread_id() const noexcept
{
    auto* thread_data_ptr = this->get_caller_thread_data();

    if (thread_data_ptr != nullptr)
    {
        std::unique_lock<std::mutex> lock{thread_data_ptr->mutex_};
        assert(thread_data_ptr->running_job_ != no_running_job && "Invalid state");
        const auto& proxy_threads =
            thread_data_ptr->jobs_[thread_data_ptr->running_job_].proxy_->threads_;
        lock.unlock();
        auto it = std::find_if(
            proxy_threads.begin(),
            proxy_threads.end(),
            [thread_data_ptr](const auto& proxy_thread)
            { return proxy_thread.thread_ == thread_data_ptr; });

        if (it != proxy_threads.end())
        {
            return it->id_;
        }
    }

    return external_thread_id;
}

/**
 * @brief Check if the calling thread belongs to the pool
 *
 * @return true if called from a pool thread executing a job, false otherwise
 */
bool parallel_thread_pool::is_parallel_scope() const noexcept
{
    return get_caller_thread_data() != nullptr;
}

/**
 * @brief Check if the calling thread is the "master" thread of its proxy
 *
 * Returns true for thread 0 of the current proxy (the first thread in
 * the proxy's thread list). Used for implementing single-threaded regions
 * within parallel code.
 *
 * @return true if this is the first thread of the current proxy, false otherwise
 */
bool parallel_thread_pool::single_thread() const
{
    auto* thread_data_ptr = get_caller_thread_data();
    if (thread_data_ptr != nullptr)
    {
        const std::scoped_lock lock{thread_data_ptr->mutex_};
        assert(thread_data_ptr->running_job_ != no_running_job && "Invalid state");
        return thread_data_ptr->jobs_[thread_data_ptr->running_job_].proxy_->threads_[0].thread_ ==
               thread_data_ptr;
    }

    return false;
}

/**
 * @brief Get the total number of physical threads in the pool
 *
 * @return Number of system threads
 */
std::size_t parallel_thread_pool::thread_count() const noexcept
{
    return threads_.size();
}

/**
 * @brief Find thread_data for the calling thread
 *
 * Searches the pool's thread list to find the thread_data structure
 * corresponding to the current std::thread.
 *
 * @return Pointer to thread_data if caller is a pool thread, nullptr otherwise
 *
 * @note This is a linear search, but pool size is typically small (< 100 threads)
 */
parallel_thread_pool::thread_data* parallel_thread_pool::get_caller_thread_data()
    const noexcept  // NOLINT(readability-convert-member-functions-to-static)
{
    for (const auto& thread_data_ptr : threads_)
    {
        if (thread_data_ptr->systethread_.get_id() == std::this_thread::get_id())
        {
            return thread_data_ptr.get();
        }
    }

    return nullptr;
}

/**
 * @brief Create a worker thread for the pool
 *
 * The worker thread runs a loop that:
 * 1. Waits for initialization to complete (initialized_ flag)
 * 2. Enters main loop waiting for jobs
 * 3. Wakes up when jobs are available or pool is shutting down
 * 4. Executes jobs from its queue (LIFO order for cache locality)
 * 5. Exits when joining_ is set and queue is empty
 *
 * @return A std::thread running the worker loop
 *
 * @note Jobs are processed in LIFO order (most recently added first)
 * @note Thread blocks on condition variable when no work is available
 */
std::thread parallel_thread_pool::make_thread()
{
    return std::thread{[this]()
                       {
                           // Wait for pool initialization to complete
                           while (!initialized_.load(std::memory_order_acquire)) {}

                           thread_data& thread_data_ref = *this->get_caller_thread_data();

                           // Main worker loop
                           while (true)
                           {
                               std::unique_lock<std::mutex> lock{thread_data_ref.mutex_};

                               // Wait for work or shutdown signal
                               thread_data_ref.condition_variable_.wait(
                                   lock,
                                   [this, &thread_data_ref]
                                   {
                                       return !thread_data_ref.jobs_.empty() ||
                                              joining_.load(std::memory_order_acquire);
                                   });

                               // Exit if shutting down and no more work
                               if (thread_data_ref.jobs_.empty())
                               {
                                   break;
                               }

                               // Execute the most recently added job (LIFO for cache locality)
                               run_job(thread_data_ref, thread_data_ref.jobs_.size() - 1, lock);
                           }
                       }};
}

/**
 * @brief Allocate additional threads for a nested proxy
 *
 * When creating a nested proxy (from within a parallel region), this function
 * finds threads that are not already allocated to any parent proxy and adds
 * them to the new proxy's thread list.
 *
 * Algorithm:
 * 1. Check if parent already has all threads (if so, can't allocate more)
 * 2. For each thread in the pool, check if it's "free"
 *    - A thread is free if it's not used by this proxy's parent or any ancestor
 * 3. Add free threads to the proxy until max_count is reached
 *
 * @param proxy The nested proxy being allocated
 * @param max_count Maximum number of threads to allocate
 *
 * @note The calling thread (thread 0) is already added before this is called
 * @note This prevents nested proxies from interfering with each other
 */
void parallel_thread_pool::fill_threads_for_nested_proxy(proxy_data* proxy, std::size_t max_count)
{
    if (proxy->parent_->threads_.size() == threads_.size())
    {
        return;  // Parent uses all threads, none available
    }

    // Lambda to check if a thread is free (not used by any ancestor proxy)
    const auto is_free = [proxy](thread_data* thread_data_ptr)
    {
        // Walk up the entire proxy hierarchy: the thread is only free if no
        // ancestor (parent, grandparent, ...) has claimed it.
        for (auto* parent = proxy->parent_; parent != nullptr; parent = parent->parent_)
        {
            const bool free_in_ancestor = std::all_of(
                parent->threads_.begin(),
                parent->threads_.end(),
                [thread_data_ptr](const auto& proxy_thread)
                { return proxy_thread.thread_ != thread_data_ptr; });
            if (!free_in_ancestor)
            {
                return false;
            }
        }

        return true;  // Thread is free
    };

    // Allocate free threads up to max_count
    for (auto& thread_data_ptr : threads_)
    {
        if (is_free(thread_data_ptr.get()))
        {
            proxy->threads_.emplace_back(thread_data_ptr.get(), this->get_next_thread_id());
        }

        if (proxy->threads_.size() == max_count)
        {
            break;  // Reached desired thread count
        }
    }
}

/**
 * @brief Get the next unique virtual thread ID
 *
 * Thread IDs are globally unique and monotonically increasing.
 * Used to assign logical IDs to threads within proxies.
 *
 * @return A new unique thread ID (starting from 2)
 *
 * @note Thread-safe via atomic fetch_add
 * @note ID 1 is reserved for external_thread_id
 */
std::size_t parallel_thread_pool::get_next_thread_id() noexcept
{
    return next_proxy_thread_id_.fetch_add(1, std::memory_order_relaxed) + 1;
}

/**
 * @brief Get the global singleton thread pool instance
 *
 * The thread pool is lazily initialized on first access and persists
 * for the lifetime of the program.
 *
 * @return Reference to the singleton instance
 *
 * @note Thread-safe due to C++11 static initialization guarantees
 * @note The pool is never destroyed (threads remain until program exit)
 */
parallel_thread_pool& parallel_thread_pool::instance()
{
    static parallel_thread_pool instance{};
    return instance;
}

}  // namespace parallel_impl
}  // namespace detail
}  // namespace parallel
