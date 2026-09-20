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

#include "threaded_callback_queue.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

//=============================================================================
class threaded_callback_queue::thread_worker
{
public:
    thread_worker(threaded_callback_queue* queue, std::shared_ptr<std::atomic_int>& thread_index)
        : queue_(queue), thread_index_(thread_index)
    {
    }

    thread_worker(thread_worker&& other) noexcept
        : queue_(other.queue_), thread_index_(std::move(other.thread_index_))
    {
    }

    void operator()()
    {
        while (this->pop()) {}
        const std::scoped_lock lock(queue_->control_mutex_);
        queue_->thread_id_to_index_.erase(std::this_thread::get_id());
    }

private:
    /**
   * Pops an invoker from the queue and runs it.
   */
    bool pop()
    {
        std::unique_lock<std::mutex> lock(queue_->mutex_);

        if (this->on_hold())
        {
            queue_->condition_variable_.wait(lock, [this] { return !this->on_hold(); });
        }

        // try_invoke() leaves nullptr holes for tasks it already ran.
        queue_->pop_front_nullptr();

        if (!this->can_continue())
        {
            return false;
        }

        auto& invoker_queue = queue_->invoker_queue_;

        const shared_future_base_pointer invoker = std::move(invoker_queue.front());
        invoker_queue.pop_front();

        invoker->status_.store(RUNNING, std::memory_order_release);

        queue_->pop_front_nullptr();
        lock.unlock();

        queue_->invoke(invoker.get());

        return true;
    }

    /**
   * Thread is on hold if conditions are met.
   */
    [[nodiscard]] bool on_hold() const
    {
        return *thread_index_ < queue_->number_of_threads_ &&
               !queue_->destroying_.load(std::memory_order_acquire) &&
               queue_->invoker_queue_.empty();
    }

    /**
   * We can continue popping elements if conditions are met.
   */
    [[nodiscard]] bool can_continue() const
    {
        return *thread_index_ < queue_->number_of_threads_ && !queue_->invoker_queue_.empty();
    }

    threaded_callback_queue*         queue_;
    std::shared_ptr<std::atomic_int> thread_index_;
};

//-----------------------------------------------------------------------------
threaded_callback_queue::threaded_callback_queue()
{
    this->set_number_of_threads(1);
}

//-----------------------------------------------------------------------------
threaded_callback_queue::~threaded_callback_queue()
{
    {
        const std::scoped_lock destroy_lock(destroy_mutex_);
        {
            const std::scoped_lock lock(mutex_);
            destroying_ = true;
        }
    }

    condition_variable_.notify_all();
    this->sync();
}

//-----------------------------------------------------------------------------
void threaded_callback_queue::set_number_of_threads(int number_of_threads)
{
    this->push_control(
        [this, number_of_threads]()
        {
            const int size = static_cast<int>(threads_.size());

            const std::scoped_lock destroy_lock(destroy_mutex_);
            if (destroying_)
            {
                return;
            }
            if (size == number_of_threads)
            {
                return;
            }
            if (size < number_of_threads)
            {
                number_of_threads_ = number_of_threads;

                std::generate_n(
                    std::back_inserter(threads_),
                    number_of_threads - size,
                    [this]
                    {
                        auto thread_index =
                            std::make_shared<std::atomic_int>(static_cast<int>(threads_.size()));
                        auto thread = std::thread(thread_worker(this, thread_index));
                        {
                            const std::scoped_lock thread_id_lock(thread_id_to_index_mutex_);
                            thread_id_to_index_.emplace(thread.get_id(), thread_index);
                        }
                        return thread;
                    });
            }
            else
            {
                {
                    std::unique_lock<std::mutex> lock(thread_id_to_index_mutex_);
                    auto self = thread_id_to_index_.find(std::this_thread::get_id());
                    if (self != thread_id_to_index_.end())
                    {
                        std::atomic_int& thread_index = *self->second;
                        if (thread_index && thread_index >= number_of_threads)
                        {
                            auto t0 = thread_id_to_index_.find(threads_[0].get_id());
                            if (t0 != thread_id_to_index_.end())
                            {
                                std::atomic_int& thread0_index = *t0->second;
                                lock.unlock();

                                std::swap(threads_[thread_index], threads_[0]);

                                const int tmp = thread0_index;
                                thread0_index.exchange(thread_index);
                                thread_index = tmp;
                            }
                        }
                    }
                }

                {
                    const std::scoped_lock lock(mutex_);
                    number_of_threads_ = number_of_threads;
                }
                condition_variable_.notify_all();
                this->sync(number_of_threads_);

                threads_.resize(number_of_threads);
            }
        });
}

//-----------------------------------------------------------------------------
void threaded_callback_queue::sync(int start_id)
{
    std::for_each(
        threads_.begin() + start_id, threads_.end(), [](std::thread& thread) { thread.join(); });
}

//-----------------------------------------------------------------------------
void threaded_callback_queue::pop_front_nullptr()
{
    while (!invoker_queue_.empty() && !invoker_queue_.front())
    {
        invoker_queue_.pop_front();
    }
}

//-----------------------------------------------------------------------------
void threaded_callback_queue::invoke(shared_future_base* invoker)
{
    (*invoker)();
    this->signal_dependent_shared_futures(invoker);
}

//-----------------------------------------------------------------------------
void threaded_callback_queue::signal_dependent_shared_futures(shared_future_base* invoker)
{
    std::vector<shared_future_base_pointer> invokers_to_launch;
    {
        const std::scoped_lock lock(invoker->mutex_);

        for (auto& dependent : invoker->dependents_)
        {
            std::unique_lock<std::mutex> dependent_lock(dependent->mutex_);
            --dependent->number_of_prior_shared_futures_remaining_;
            if (dependent->status_.load(std::memory_order_acquire) == ON_HOLD &&
                (dependent->number_of_prior_shared_futures_remaining_ == 0))
            {
                if (dependent->is_high_priority_)
                {
                    dependent->status_.store(RUNNING, std::memory_order_release);
                    dependent_lock.unlock();
                    this->invoke(dependent.get());
                }
                else
                {
                    dependent_lock.unlock();
                    invokers_to_launch.emplace_back(std::move(dependent));
                }
            }
        }
    }

    if (!invokers_to_launch.empty())
    {
        const std::scoped_lock lock(mutex_);
        this->pop_front_nullptr();
        std::int64_t index = invoker_queue_.empty()
                                 ? static_cast<std::int64_t>(invokers_to_launch.size())
                                 : this->front_invoker_index();
        for (shared_future_base_pointer& inv : invokers_to_launch)
        {
            assert(
                inv->status_.load(std::memory_order_acquire) == ON_HOLD &&
                "Status should be ON_HOLD");
            inv->invoker_index_ = --index;

            const std::scoped_lock state_lock(inv->mutex_);
            inv->status_.store(ENQUEUED, std::memory_order_release);
            invoker_queue_.emplace_front(std::move(inv));
        }
    }

    for (std::size_t i = 0; i < invokers_to_launch.size(); ++i)
    {
        condition_variable_.notify_one();
    }
}

//-----------------------------------------------------------------------------
bool threaded_callback_queue::try_invoke(shared_future_base* invoker)
{
    if (![this, &invoker]
        {
            if (invoker->status_.load(std::memory_order_relaxed) != ENQUEUED)
            {
                return false;
            }

            const std::scoped_lock lock(mutex_);

            this->pop_front_nullptr();

            if (invoker_queue_.empty())
            {
                return false;
            }

            const std::scoped_lock inv_lock(invoker->mutex_);

            if (invoker->status_.load(std::memory_order_acquire) != ENQUEUED)
            {
                return false;
            }

            const std::int64_t index =
                invoker->invoker_index_ - invoker_queue_.front()->invoker_index_;

            // A negative index means another worker already picked this invoker
            // (VTK checks vtkIdType < 0). Unsigned wrap used to index off the
            // deque and corrupt later mutexes.
            if (index < 0 || static_cast<std::size_t>(index) >= invoker_queue_.size())
            {
                return false;
            }

            shared_future_base_pointer& result = invoker_queue_[static_cast<std::size_t>(index)];

            if (result.get() != invoker)
            {
                return false;
            }

            if (index == 0)
            {
                invoker_queue_.pop_front();
                this->pop_front_nullptr();
            }
            else
            {
                // Hole so a worker does not pop and re-run a task that
                // wait() already executed. Re-running after the caller has
                // destroyed captured stack state throws
                // "mutex lock failed: Invalid argument" on macOS libc++.
                result.reset();
            }
            invoker->status_.store(RUNNING, std::memory_order_release);
            return true;
        }())
    {
        return false;
    }

    this->invoke(invoker);
    return true;
}
