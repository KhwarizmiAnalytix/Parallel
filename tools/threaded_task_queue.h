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
 * @class threaded_task_queue
 * @brief simple threaded task queue
 *
 * threaded_task_queue provides a simple task queue that can use threads to
 * execute individual tasks. It is intended for use applications such as data
 * compression, encoding etc. where the task may be completed concurrently
 * without blocking the main thread.
 *
 * threaded_task_queue's API is intended to be called from the same main thread.
 * The constructor defines the work (or task) to be performed. `push` allows the
 * caller to enqueue a task with specified input arguments. The call will return
 * immediately without blocking. The task is enqueued and will be executed
 * concurrently when resources become available. `pop` will block until the
 * result is available. To avoid waiting for results to be available, use
 * `try_pop`.
 *
 * The constructor allows mechanism to customize the queue. `strict_ordering`
 * implies that results should be popped in the same order that tasks were
 * pushed without dropping any task. If the caller is only concerned with
 * obtaining the latest available result where intermediate results that take
 * longer to compute may be dropped, then `strict_ordering` can be set to `false`.
 *
 * `max_concurrent_tasks` controls how many threads are used to process tasks in
 * the queue. Default is same as `multi_threader::get_global_default_number_of_threads()`.
 *
 * `buffer_size` indicates how many tasks may be queued for processing. Default
 * is infinite size. If a positive number is provided, then pushing additional
 * tasks will result in discarding of older tasks that haven't begun processing
 * from the queue. Note, this does not impact tasks that may already be in
 * progress. Also, if `strict_ordering` is true, this is ignored; the
 * buffer_size will be set to unlimited.
 */

#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "common/parallel_export.h"
#include "multi_threader.h"

// Undefine min/max macros from Windows headers if they were defined
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS
namespace threaded_task_queue_internals
{

template <typename R>
class task_queue;

template <typename R>
class result_queue;

}  // namespace threaded_task_queue_internals
#endif  // DOXYGEN_SHOULD_SKIP_THIS

template <typename R, typename... Args>
class PARALLEL_VISIBILITY threaded_task_queue
{
public:
    threaded_task_queue(
        std::function<R(Args...)> worker,
        bool                      strict_ordering      = true,
        int                       buffer_size          = -1,
        int                       max_concurrent_tasks = -1);
    ~threaded_task_queue();

    /**
   * Push arguments for the work
   */
    void push(Args&&... args);

    /**
   * Pop the last result. Returns true on success. May fail if called on an
   * empty queue. This will wait for result to be available.
   */
    bool pop(R& result);

    /**
   * Attempt to pop without waiting. If no results are available, returns
   * false.
   */
    bool try_pop(R& result);

    /**
   * Returns false if there's some result that may be popped right now or in the
   * future.
   */
    bool is_empty() const;

    /**
   * Blocks till the queue becomes empty.
   */
    void flush();

private:
    threaded_task_queue(const threaded_task_queue&) = delete;
    void operator=(const threaded_task_queue&)      = delete;

    std::function<R(Args...)>                                       worker_;
    std::unique_ptr<threaded_task_queue_internals::task_queue<R>>   tasks_;
    std::unique_ptr<threaded_task_queue_internals::result_queue<R>> results_;
    int                                                             number_of_threads_;
    std::unique_ptr<std::thread[]>                                  threads_;
};

template <typename... Args>
class PARALLEL_VISIBILITY threaded_task_queue<void, Args...>
{
public:
    threaded_task_queue(
        std::function<void(Args...)> worker,
        bool                         strict_ordering      = true,
        int                          buffer_size          = -1,
        int                          max_concurrent_tasks = -1);
    ~threaded_task_queue();

    /**
   * Push arguments for the work
   */
    void push(Args&&... args);

    /**
   * Returns false if there's some result that may be popped right now or in the
   * future.
   */
    bool is_empty() const;

    /**
   * Blocks till the queue becomes empty.
   */
    void flush();

private:
    threaded_task_queue(const threaded_task_queue&) = delete;
    void operator=(const threaded_task_queue&)      = delete;

    std::function<void(Args...)>                                     worker_;
    std::unique_ptr<threaded_task_queue_internals::task_queue<void>> tasks_;
    std::condition_variable                                          results_cv_;
    std::mutex                                                       next_result_id_mutex_;
    std::atomic<std::uint64_t>                                       next_result_id_;
    int                                                              number_of_threads_;
    std::unique_ptr<std::thread[]>                                   threads_;
};

// ========== Template Implementation ==========

#ifndef DOXYGEN_SHOULD_SKIP_THIS
namespace threaded_task_queue_internals
{

template <typename R>
class task_queue
{
public:
    task_queue(int buffer_size) : done_(false), buffer_size_(buffer_size), next_task_id_(0) {}

    ~task_queue() = default;

    void mark_done()
    {
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            done_ = true;
        }
        tasks_cv_.notify_all();
    }

    std::uint64_t get_next_task_id() const { return next_task_id_; }

    void push(std::function<R()>&& task)
    {
        if (done_)
        {
            return;
        }
        else
        {
            std::lock_guard<std::mutex> lk(tasks_mutex_);
            tasks_.push(std::make_pair(next_task_id_++, std::move(task)));
            while (buffer_size_ > 0 && static_cast<int>(tasks_.size()) > buffer_size_)
            {
                tasks_.pop();
            }
        }
        tasks_cv_.notify_one();
    }

    bool pop(std::uint64_t& task_id, std::function<R()>& task)
    {
        std::unique_lock<std::mutex> lk(tasks_mutex_);
        tasks_cv_.wait(lk, [this] { return done_ || !tasks_.empty(); });
        if (!tasks_.empty())
        {
            auto task_pair = tasks_.front();
            tasks_.pop();
            lk.unlock();

            task_id = task_pair.first;
            task    = std::move(task_pair.second);
            return true;
        }
        assert(done_);
        return false;
    }

private:
    std::atomic_bool                                         done_;
    int                                                      buffer_size_;
    std::atomic<std::uint64_t>                               next_task_id_;
    std::queue<std::pair<std::uint64_t, std::function<R()>>> tasks_;
    std::mutex                                               tasks_mutex_;
    std::condition_variable                                  tasks_cv_;
};

//=============================================================================
template <typename R>
class result_queue
{
public:
    result_queue(bool strict_ordering) : next_result_id_(0), strict_ordering_(strict_ordering) {}

    ~result_queue() = default;

    std::uint64_t get_next_result_id() const { return next_result_id_; }

    void push(std::uint64_t task_id, R&& result)
    {
        std::unique_lock<std::mutex> lk(results_mutex_);
        if (task_id >= next_result_id_)
        {
            results_.push(std::make_pair(task_id, std::move(result)));
        }
        lk.unlock();
        results_cv_.notify_one();
    }

    bool try_pop(R& result)
    {
        std::unique_lock<std::mutex> lk(results_mutex_);
        if (results_.empty() || (strict_ordering_ && results_.top().first != next_result_id_))
        {
            return false;
        }

        auto result_pair = results_.top();
        next_result_id_  = (result_pair.first + 1);
        results_.pop();
        lk.unlock();

        result = std::move(result_pair.second);
        return true;
    }

    bool pop(R& result)
    {
        std::unique_lock<std::mutex> lk(results_mutex_);
        results_cv_.wait(
            lk,
            [this]
            {
                return !results_.empty() &&
                       (!strict_ordering_ || results_.top().first == next_result_id_);
            });
        lk.unlock();
        return this->try_pop(result);
    }

private:
    template <typename T>
    struct comparator
    {
        bool operator()(const T& left, const T& right) const { return left.first > right.first; }
    };
    std::priority_queue<
        std::pair<std::uint64_t, R>,
        std::vector<std::pair<std::uint64_t, R>>,
        comparator<std::pair<std::uint64_t, R>>>
                               results_;
    std::mutex                 results_mutex_;
    std::condition_variable    results_cv_;
    std::atomic<std::uint64_t> next_result_id_;
    bool                       strict_ordering_;
};

}  // namespace threaded_task_queue_internals
#endif  // DOXYGEN_SHOULD_SKIP_THIS

//-----------------------------------------------------------------------------
template <typename R, typename... Args>
threaded_task_queue<R, Args...>::threaded_task_queue(
    std::function<R(Args...)> worker,
    bool                      strict_ordering,
    int                       buffer_size,
    int                       max_concurrent_tasks)
    : worker_(worker),
      tasks_(new threaded_task_queue_internals::task_queue<R>(
          std::max(0, strict_ordering ? 0 : buffer_size))),
      results_(new threaded_task_queue_internals::result_queue<R>(strict_ordering)),
      number_of_threads_(
          max_concurrent_tasks <= 0 ? multi_threader::get_global_default_number_of_threads()
                                    : max_concurrent_tasks),
      threads_{new std::thread[number_of_threads_]}
{
    auto f = [this](int thread_id)
    {
        while (true)
        {
            std::function<R()> task;
            std::uint64_t      task_id;
            if (tasks_->pop(task_id, task))
            {
                results_->push(task_id, task());
                continue;
            }
            else
            {
                break;
            }
        }
    };

    for (int cc = 0; cc < number_of_threads_; ++cc)
    {
        threads_[cc] = std::thread(f, cc);
    }
}

//-----------------------------------------------------------------------------
template <typename R, typename... Args>
threaded_task_queue<R, Args...>::~threaded_task_queue()
{
    tasks_->mark_done();
    for (int cc = 0; cc < number_of_threads_; ++cc)
    {
        threads_[cc].join();
    }
}

//-----------------------------------------------------------------------------
template <typename R, typename... Args>
void threaded_task_queue<R, Args...>::push(Args&&... args)
{
    tasks_->push([this, arguments = std::make_tuple(std::forward<Args>(args)...)]()
                 { return std::apply(worker_, arguments); });
}

//-----------------------------------------------------------------------------
template <typename R, typename... Args>
bool threaded_task_queue<R, Args...>::try_pop(R& result)
{
    return results_->try_pop(result);
}

//-----------------------------------------------------------------------------
template <typename R, typename... Args>
bool threaded_task_queue<R, Args...>::pop(R& result)
{
    if (this->is_empty())
    {
        return false;
    }

    return results_->pop(result);
}

//-----------------------------------------------------------------------------
template <typename R, typename... Args>
bool threaded_task_queue<R, Args...>::is_empty() const
{
    return results_->get_next_result_id() == tasks_->get_next_task_id();
}

//-----------------------------------------------------------------------------
template <typename R, typename... Args>
void threaded_task_queue<R, Args...>::flush()
{
    R tmp;
    while (!this->is_empty())
    {
        this->pop(tmp);
    }
}

//=============================================================================
// ** specialization for `void` return types.
//=============================================================================

//-----------------------------------------------------------------------------
template <typename... Args>
threaded_task_queue<void, Args...>::threaded_task_queue(
    std::function<void(Args...)> worker,
    bool                         strict_ordering,
    int                          buffer_size,
    int                          max_concurrent_tasks)
    : worker_(worker),
      tasks_(new threaded_task_queue_internals::task_queue<void>(
          std::max(0, strict_ordering ? 0 : buffer_size))),
      next_result_id_(0),
      number_of_threads_(
          max_concurrent_tasks <= 0 ? multi_threader::get_global_default_number_of_threads()
                                    : max_concurrent_tasks),
      threads_{new std::thread[number_of_threads_]}
{
    auto f = [this](int thread_id)
    {
        while (true)
        {
            std::function<void()> task;
            std::uint64_t         task_id;
            if (tasks_->pop(task_id, task))
            {
                task();

                std::unique_lock<std::mutex> lk(next_result_id_mutex_);
                next_result_id_ =
                    std::max(static_cast<std::uint64_t>(next_result_id_), task_id + 1);
                lk.unlock();
                results_cv_.notify_all();
                continue;
            }
            else
            {
                break;
            }
        }
        results_cv_.notify_all();
    };

    for (int cc = 0; cc < number_of_threads_; ++cc)
    {
        threads_[cc] = std::thread(f, cc);
    }
}

//-----------------------------------------------------------------------------
template <typename... Args>
threaded_task_queue<void, Args...>::~threaded_task_queue()
{
    tasks_->mark_done();
    for (int cc = 0; cc < number_of_threads_; ++cc)
    {
        threads_[cc].join();
    }
}

//-----------------------------------------------------------------------------
template <typename... Args>
void threaded_task_queue<void, Args...>::push(Args&&... args)
{
    tasks_->push([this, arguments = std::make_tuple(std::forward<Args>(args)...)]()
                 { std::apply(worker_, arguments); });
}

//-----------------------------------------------------------------------------
template <typename... Args>
bool threaded_task_queue<void, Args...>::is_empty() const
{
    return next_result_id_ == tasks_->get_next_task_id();
}

//-----------------------------------------------------------------------------
template <typename... Args>
void threaded_task_queue<void, Args...>::flush()
{
    if (this->is_empty())
    {
        return;
    }
    std::unique_lock<std::mutex> lk(next_result_id_mutex_);
    results_cv_.wait(lk, [this] { return this->is_empty(); });
}
