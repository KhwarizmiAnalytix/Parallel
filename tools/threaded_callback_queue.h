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
 * @class threaded_callback_queue
 * @brief simple threaded callback queue
 *
 * This callback queue executes pushed functions and functors on threads whose
 * purpose is to execute those functions.
 * By default, one thread is created by this class, so it is advised to set `number_of_threads_`.
 * Upon destruction of an instance of this callback queue, remaining unexecuted threads are
 * executed.
 *
 * When a task is pushed, a `shared_future` is returned. This instance can be used to get the
 * returned value when the task is finished, and provides functionalities to synchronize the main
 * thread with the status of its associated task.
 *
 * All public methods of this class are thread safe.
 */

#pragma once

#include <algorithm>           // For any_of
#include <array>               // For array
#include <atomic>              // For atomic_bool
#include <cassert>             // For assert
#include <condition_variable>  // For condition variable
#include <cstdint>             // For int64_t invoker indices
#include <deque>               // For deque
#include <functional>          // For greater
#include <memory>              // For unique_ptr, shared_ptr
#include <mutex>               // For mutex
#if __cplusplus >= 202002L
#include <ranges>  // For views::reverse
#endif
#include <thread>         // For thread
#include <tuple>          // For tuple
#include <type_traits>    // For type_traits
#include <unordered_map>  // For unordered_map
#include <unordered_set>  // For unordered_set
#include <utility>        // For forward
#include <vector>         // For vector

#include "common/parallel_export.h"

class PARALLEL_VISIBILITY threaded_callback_queue
{
private:
    /**
   * Helper to extract the parameter types of a function (passed by template)
   */
    template <class FT>
    struct signature;

    /**
   * Helper that dereferences the input type `T`. If `T == Object`, or
   * `T == Object*` or `T == std::unique_ptr<Object>`, then `Type` is of type `Object`.
   */
    template <class T, class DummyT = std::nullptr_t>
    struct dereference
    {
        struct type;
    };

    /**
   * Convenient typedef to help lighten the code.
   */
    template <class T>
    using dereferenced_type = typename std::decay<typename dereference<T>::type>::type;

    /**
   * This resolves to the return type of the template function `FT`.
   */
    template <class FT>
    using invoke_result = typename signature<dereferenced_type<FT>>::invoke_result;

    /**
   * This class wraps the returned value and gives access to it.
   */
    template <class ReturnT, bool IsLValueReference = std::is_lvalue_reference<ReturnT>::value>
    struct return_value_wrapper;

public:
    PARALLEL_API virtual ~threaded_callback_queue();

    PARALLEL_API threaded_callback_queue();

    /**
   * `shared_future_base` is the base block to store, run, get the returned value of the tasks that
   * are pushed in the queue.
   */
    class PARALLEL_VISIBILITY shared_future_base
    {
    public:
        shared_future_base()
            : status_(CONSTRUCTING), number_of_prior_shared_futures_remaining_(0), invoker_index_(0)
        {
        }

        virtual ~shared_future_base() = default;

        /**
     * Blocks current thread until the task associated with this future has terminated.
     */
        PARALLEL_API virtual void wait() const
        {
            if (status_.load(std::memory_order_acquire) == READY)
            {
                return;
            }
            std::unique_lock<std::mutex> lock(mutex_);
            if (status_.load(std::memory_order_acquire) != READY)
            {
                condition_variable_.wait(
                    lock, [this] { return status_.load(std::memory_order_acquire) == READY; });
            }
        }

        friend class threaded_callback_queue;

    protected:
        /**
     * Exclusive binary mask giving the status of the current invoker sharing this state.
     */
        std::atomic_int status_;

        /**
     * List of futures which are depending on us.
     */
        std::vector<std::shared_ptr<shared_future_base>> dependents_;

        mutable std::mutex              mutex_;
        mutable std::condition_variable condition_variable_;

    private:
        /**
     * This runs the stored task.
     */
        virtual void operator()() = 0;

        /**
     * Number of futures that need to terminate before we can run.
     */
        std::atomic_int number_of_prior_shared_futures_remaining_;

        /**
     * Index that is set by the invoker to this shared state.
     * Signed so control tasks pushed to the front can use a lower index than the
     * current front (VTK uses vtkIdType). An unsigned type wraps to a huge
     * offset and try_invoke then indexes off the deque.
     */
        std::int64_t invoker_index_;

        /**
     * When set to true, when this invoker becomes ready, whoever picked this invoker must directly
     * run it.
     */
        bool is_high_priority_ = false;

        shared_future_base(const shared_future_base& other) = delete;
        void operator=(const shared_future_base& other)     = delete;
    };

    /**
   * A `shared_future` is an object returned by the methods `push` and `push_dependent`.
   */
    template <class ReturnT>
    class shared_future : public shared_future_base
    {
    public:
        using return_lvalue_ref = typename return_value_wrapper<ReturnT>::return_lvalue_ref;
        using return_const_lvalue_ref =
            typename return_value_wrapper<ReturnT>::return_const_lvalue_ref;

        shared_future() = default;

        /**
     * This returns the return value of the pushed function.
     */
        return_lvalue_ref get();

        /**
     * This returns the return value of the pushed function.
     */
        return_const_lvalue_ref get() const;

        friend class threaded_callback_queue;

    private:
        return_value_wrapper<ReturnT> return_value_;

        shared_future(const shared_future<ReturnT>& other)  = delete;
        void operator=(const shared_future<ReturnT>& other) = delete;
    };

    using shared_future_base_pointer = std::shared_ptr<shared_future_base>;
    template <class ReturnT>
    using shared_future_pointer = std::shared_ptr<shared_future<ReturnT>>;

    /**
   * Pushes a function f to be passed args... as arguments.
   */
    template <class FT, class... ArgsT>
    shared_future_pointer<invoke_result<FT>> push(FT&& f, ArgsT&&... args);

    /**
   * This method behaves the same way `push` does, with the addition of a container of `futures`.
   */
    template <class SharedFutureContainerT, class FT, class... ArgsT>
    shared_future_pointer<invoke_result<FT>> push_dependent(
        SharedFutureContainerT&& prior_shared_futures, FT&& f, ArgsT&&... args);

    /**
   * This method blocks the current thread until all the tasks associated with each shared future
   * inside `prior_shared_future` has terminated.
   */
    template <class SharedFutureContainerT>
    void wait(SharedFutureContainerT&& prior_shared_future);

    ///@{
    /**
   * Get the returned value from the task associated with the input future.
   */
    template <class ReturnT>
    typename shared_future<ReturnT>::return_lvalue_ref get(shared_future_pointer<ReturnT>& future);
    template <class ReturnT>
    typename shared_future<ReturnT>::return_const_lvalue_ref get(
        const shared_future_pointer<ReturnT>& future);
    ///@}

    /**
   * Sets the number of threads.
   */
    PARALLEL_API void set_number_of_threads(int number_of_threads);

    /**
   * Returns the number of allocated threads.
   */
    PARALLEL_API int get_number_of_threads() const { return number_of_threads_; }

private:
    ///@{
    /**
   * An `invoker` subclasses `shared_future`. It provides storage and capabilities to run the
   * input function with the given parameters.
   */
    template <class FT, class... ArgsT>
    class invoker;
    ///@}

    struct invoker_impl;

    template <class FT, class... ArgsT>
    using invoker_pointer = std::shared_ptr<invoker<FT, ArgsT...>>;

    class thread_worker;

    friend class thread_worker;

    /**
   * Status that an invoker can be in.
   */
    enum status
    {
        CONSTRUCTING = 0x00,
        ON_HOLD      = 0x01,
        ENQUEUED     = 0x02,
        RUNNING      = 0x04,
        READY        = 0x08
    };

    PARALLEL_API void sync(int start_id = 0);
    PARALLEL_API void pop_front_nullptr();
    PARALLEL_API void signal_dependent_shared_futures(shared_future_base* invoker);

    template <class SharedFutureContainerT, class InvokerT>
    void handle_dependent_invoker(
        SharedFutureContainerT&& prior_shared_futures, InvokerT&& invoker);

    PARALLEL_API void invoke(shared_future_base* invoker);
    PARALLEL_API bool try_invoke(shared_future_base* invoker);

    /**
     * Index of the first/last non-null queued invoker. try_invoke() can
     * leave nullptr holes; callers must not dereference front()/back().
    */
    std::int64_t front_invoker_index() const
    {
        const auto it = std::find_if(
            invoker_queue_.begin(),
            invoker_queue_.end(),
            [](const auto& inv) { return static_cast<bool>(inv); });
        if (it != invoker_queue_.end())
        {
            return (*it)->invoker_index_;
        }
        return 0;
    }
    std::int64_t back_invoker_index() const
    {
#if __cplusplus >= 202002L
        const auto reverse_invokers = std::views::reverse(invoker_queue_);
        const auto it               = std::find_if(
            reverse_invokers.begin(),
            reverse_invokers.end(),
            [](const auto& inv) { return static_cast<bool>(inv); });
        if (it != reverse_invokers.end())
        {
            return (*it)->invoker_index_;
        }
#else
        const auto it = std::find_if(
            invoker_queue_.rbegin(),
            invoker_queue_.rend(),
            [](const auto& inv) { return static_cast<bool>(inv); });
        if (it != invoker_queue_.rend())
        {
            return (*it)->invoker_index_;
        }
#endif
        return -1;
    }

    template <class FT, class... ArgsT>
    void push_control(FT&& f, ArgsT&&... args);

    template <class SharedFutureContainerT>
    static bool must_wait(SharedFutureContainerT&& prior_shared_futures);

    std::deque<shared_future_base_pointer>                                invoker_queue_;
    std::mutex                                                            mutex_;
    std::mutex                                                            control_mutex_;
    std::mutex                                                            destroy_mutex_;
    std::mutex                                                            thread_id_to_index_mutex_;
    std::condition_variable                                               condition_variable_;
    std::atomic_bool                                                      destroying_{false};
    std::atomic_int                                                       number_of_threads_;
    std::vector<std::thread>                                              threads_;
    std::unordered_map<std::thread::id, std::shared_ptr<std::atomic_int>> thread_id_to_index_;
    std::unordered_set<shared_future_base_pointer>                        control_futures_;

    threaded_callback_queue(const threaded_callback_queue&) = delete;
    void operator=(const threaded_callback_queue&)          = delete;
};

// ========== Template Implementation ==========

//-----------------------------------------------------------------------------
template <>
struct threaded_callback_queue::return_value_wrapper<void, false>
{
    using return_lvalue_ref       = void;
    using return_const_lvalue_ref = void;

    void get() const {}
};

//=============================================================================
template <class ReturnT>
struct threaded_callback_queue::return_value_wrapper<ReturnT, true /* IsLValueReference */>
{
    using return_value_impl       = return_value_wrapper<ReturnT, false>;
    using return_lvalue_ref       = ReturnT&;
    using return_const_lvalue_ref = const ReturnT&;

    return_value_wrapper() = default;
    return_value_wrapper(ReturnT& value)
        : value_(std::unique_ptr<return_value_impl>(new return_value_impl(value)))
    {
    }

    // cppcheck-suppress returnTempReference
    ReturnT& get() { return value_->get(); }
    // cppcheck-suppress returnTempReference
    const ReturnT& get() const { return value_->get(); }

    std::unique_ptr<return_value_impl> value_;
};

//=============================================================================
template <class ReturnT>
struct threaded_callback_queue::return_value_wrapper<ReturnT, false /* IsLValueReference */>
{
    using return_lvalue_ref       = ReturnT&;
    using return_const_lvalue_ref = const ReturnT&;

    return_value_wrapper() = default;
    template <class ReturnTT>
    return_value_wrapper(ReturnTT&& value) : value_(std::forward<ReturnTT>(value))  //NOLINT
    {
    }

    ReturnT&       get() { return value_; }
    const ReturnT& get() const { return value_; }

    ReturnT value_;
};

//-----------------------------------------------------------------------------
template <class ReturnT>
typename threaded_callback_queue::shared_future<ReturnT>::return_lvalue_ref
threaded_callback_queue::shared_future<ReturnT>::get()
{
    this->wait();
    return return_value_.get();
}

//-----------------------------------------------------------------------------
template <class ReturnT>
typename threaded_callback_queue::shared_future<ReturnT>::return_const_lvalue_ref
threaded_callback_queue::shared_future<ReturnT>::get() const
{
    this->wait();
    return return_value_.get();
}

//=============================================================================
struct threaded_callback_queue::invoker_impl
{
    template <std::size_t... Is>
    struct integer_sequence;
    template <std::size_t N, std::size_t... Is>
    struct make_integer_sequence;

    template <class T>
    static decltype(*std::declval<T&>(), std::true_type{}) can_be_dereferenced(std::nullptr_t);
    template <class>
    static std::false_type can_be_dereferenced(...);

    template <class T, class CanBeDereferencedT = decltype(can_be_dereferenced<T>(nullptr))>
    struct dereference_impl;

    template <class FT, std::size_t N = signature<typename std::decay<FT>::type>::args_size>
    using args_tuple = typename signature<typename std::decay<FT>::type>::args_tuple;

    template <class FT, std::size_t I>
    using arg_type = typename std::tuple_element<I, args_tuple<FT>>::type;

    template <class FunctionArgsTupleT, class InputArgsTupleT, std::size_t... Is>
    static std::tuple<typename std::conditional<
        std::is_lvalue_reference<typename std::tuple_element<Is, FunctionArgsTupleT>::type>::value,
        typename std::decay<typename std::tuple_element<Is, FunctionArgsTupleT>::type>::type,
        typename std::decay<typename std::tuple_element<Is, InputArgsTupleT>::type>::type>::type...>
        get_static_cast_args_tuple(integer_sequence<Is...>);

    template <class FunctionArgsTupleT, class... InputArgsT>
    using static_cast_args_tuple =
        decltype(get_static_cast_args_tuple<FunctionArgsTupleT, std::tuple<InputArgsT...>>(
            make_integer_sequence<sizeof...(InputArgsT)>()));

    template <bool IsMemberFunctionPointer, class... ArgsT>
    class invoker_handle;

    template <class ReturnT>
    struct invoker_helper
    {
        template <class InvokerT>
        static void invoke(InvokerT&& invoker, shared_future<ReturnT>* future)
        {
            {
                std::scoped_lock<std::mutex> lock(future->mutex_);
                future->return_value_ = return_value_wrapper<ReturnT>(invoker());
                future->status_.store(READY, std::memory_order_release);
            }
            future->condition_variable_.notify_all();
        }
    };
};

//=============================================================================
template <>
struct threaded_callback_queue::invoker_impl::invoker_helper<void>
{
    template <class InvokerT>
    static void invoke(InvokerT&& invoker, shared_future<void>* future)
    {
        invoker();
        {
            std::scoped_lock<std::mutex> lock(future->mutex_);
            future->status_.store(READY, std::memory_order_release);
        }
        future->condition_variable_.notify_all();
    }
};

//=============================================================================
// For lambdas or std::function
template <class ReturnT, class... ArgsT>
struct threaded_callback_queue::signature<ReturnT(ArgsT...)>
{
    using args_tuple                       = std::tuple<ArgsT...>;
    using invoke_result                    = ReturnT;
    static constexpr std::size_t args_size = sizeof...(ArgsT);
};

//=============================================================================
// For methods inside a class ClassT
template <class ClassT, class ReturnT, class... ArgsT>
struct threaded_callback_queue::signature<ReturnT (ClassT::*)(ArgsT...)>
{
    using args_tuple                       = std::tuple<ArgsT...>;
    using invoke_result                    = ReturnT;
    static constexpr std::size_t args_size = sizeof...(ArgsT);
};

//=============================================================================
// For const methods inside a class ClassT
template <class ClassT, class ReturnT, class... ArgsT>
struct threaded_callback_queue::signature<ReturnT (ClassT::*)(ArgsT...) const>
{
    using args_tuple                       = std::tuple<ArgsT...>;
    using invoke_result                    = ReturnT;
    static constexpr std::size_t args_size = sizeof...(ArgsT);
};

//=============================================================================
// For function pointers
template <class ReturnT, class... ArgsT>
struct threaded_callback_queue::signature<ReturnT (*)(ArgsT...)>
{
    using args_tuple                       = std::tuple<ArgsT...>;
    using invoke_result                    = ReturnT;
    static constexpr std::size_t args_size = sizeof...(ArgsT);
};

//=============================================================================
// For function pointers
template <class ReturnT, class... ArgsT>
struct threaded_callback_queue::signature<ReturnT (&)(ArgsT...)>
{
    using args_tuple                       = std::tuple<ArgsT...>;
    using invoke_result                    = ReturnT;
    static constexpr std::size_t args_size = sizeof...(ArgsT);
};

//=============================================================================
// For functors
template <class FT>
struct threaded_callback_queue::signature
    : threaded_callback_queue::signature<decltype(&FT::operator())>
{
};

//=============================================================================
template <std::size_t... Is>
struct threaded_callback_queue::invoker_impl::integer_sequence
{
};

//=============================================================================
template <std::size_t N, std::size_t... Is>
struct threaded_callback_queue::invoker_impl::make_integer_sequence
    : threaded_callback_queue::invoker_impl::make_integer_sequence<N - 1, N - 1, Is...>
{
};

//=============================================================================
template <std::size_t... Is>
struct threaded_callback_queue::invoker_impl::make_integer_sequence<0, Is...>
    : threaded_callback_queue::invoker_impl::integer_sequence<Is...>
{
};

//=============================================================================
template <class T>
struct threaded_callback_queue::invoker_impl::dereference_impl<T, std::true_type>
{
    using type = typename std::remove_pointer<decltype(*std::declval<T>())>::type;
    // cppcheck-suppress constParameterReference
    static type& get(T& instance) { return *instance; }
};

//=============================================================================
template <class T>
struct threaded_callback_queue::invoker_impl::dereference_impl<T, std::false_type>
{
    using type = T;
    static type& get(T& instance) { return instance; }
};

//=============================================================================
template <class T>
struct threaded_callback_queue::dereference<T, std::nullptr_t>
{
    using type = typename invoker_impl::dereference_impl<T>::type;
};

//=============================================================================
template <class FT, class ObjectT, class... ArgsT>
class threaded_callback_queue::invoker_impl::
    invoker_handle<true /* IsMemberFunctionPointer */, FT, ObjectT, ArgsT...>
{
public:
    template <class FTT, class ObjectTT, class... ArgsTT>
    invoker_handle(FTT&& f, ObjectTT&& instance, ArgsTT&&... args)
        : function_(std::forward<FTT>(f)),
          instance_(std::forward<ObjectTT>(instance)),
          args_(std::forward<ArgsTT>(args)...)
    {
    }

    invoke_result<FT> operator()()
    {
        return this->invoke_impl(make_integer_sequence<sizeof...(ArgsT)>());
    }

private:
    template <std::size_t... Is>
    invoke_result<FT> invoke_impl(integer_sequence<Is...>)
    {
        auto& deref = dereference_impl<ObjectT>::get(instance_);
        return (deref.*function_)(static_cast<arg_type<FT, Is>>(std::get<Is>(args_))...);
    }

    FT                                               function_;
    typename std::decay<ObjectT>::type               instance_;
    static_cast_args_tuple<args_tuple<FT>, ArgsT...> args_;
};

//=============================================================================
template <class FT, class... ArgsT>
class threaded_callback_queue::invoker_impl::
    invoker_handle<false /* IsMemberFunctionPointer */, FT, ArgsT...>
{
public:
    template <class FTT, class... ArgsTT>
    invoker_handle(FTT&& f, ArgsTT&&... args)
        : function_(std::forward<FTT>(f)), args_(std::forward<ArgsTT>(args)...)
    {
    }

    invoke_result<FT> operator()()
    {
        return this->invoke_impl(make_integer_sequence<sizeof...(ArgsT)>());
    }

private:
    template <std::size_t... Is>
    invoke_result<FT> invoke_impl(integer_sequence<Is...>)
    {
        // cppcheck-suppress constVariableReference
        auto& function = dereference_impl<FT>::get(function_);
        return function(static_cast<arg_type<decltype(function), Is>>(std::get<Is>(args_))...);
    }

    typename std::decay<FT>::type                                                function_;
    static_cast_args_tuple<args_tuple<typename dereference<FT>::type>, ArgsT...> args_;
};

//=============================================================================
template <class FT, class... ArgsT>
class threaded_callback_queue::invoker
    : public threaded_callback_queue::shared_future<threaded_callback_queue::invoke_result<FT>>
{
public:
    template <class... ArgsTT>
    static invoker<FT, ArgsT...>* create(ArgsTT&&... args)
    {
        return new invoker<FT, ArgsT...>(std::forward<ArgsTT>(args)...);
    }

    template <class... ArgsTT>
    invoker(ArgsTT&&... args) : impl_(std::forward<ArgsTT>(args)...)
    {
    }

    void operator()() override
    {
        assert(
            this->status_.load(std::memory_order_acquire) == RUNNING && "Status should be RUNNING");
        invoker_impl::invoker_helper<invoke_result<FT>>::invoke(impl_, this);
    }

    friend class threaded_callback_queue;

private:
    invoker_impl::invoker_handle<std::is_member_function_pointer<FT>::value, FT, ArgsT...> impl_;

    invoker(const invoker<FT, ArgsT...>& other)        = delete;
    void operator=(const invoker<FT, ArgsT...>& other) = delete;
};

//-----------------------------------------------------------------------------
// Helper functions to extract raw pointer from either raw pointer or shared_ptr
namespace detail
{
template <typename T>
inline T* get_raw_ptr(T* ptr)
{
    return ptr;
}

template <typename T>
inline T* get_raw_ptr(const std::shared_ptr<T>& ptr)
{
    return ptr.get();
}
}  // namespace detail

//-----------------------------------------------------------------------------
template <class SharedFutureContainerT, class InvokerT>
void threaded_callback_queue::handle_dependent_invoker(
    SharedFutureContainerT&& prior_shared_futures, InvokerT&& invoker)
{
    if (!prior_shared_futures.empty())
    {
        for (const auto& future_item : prior_shared_futures)
        {
            shared_future_base* prior = detail::get_raw_ptr(future_item);
            if (prior->status_.load(std::memory_order_acquire) == READY)
            {
                continue;
            }

            std::unique_lock<std::mutex> lock(prior->mutex_);
            if (prior->status_.load(std::memory_order_acquire) != READY)
            {
                prior->dependents_.emplace_back(invoker);
                lock.unlock();
                invoker->number_of_prior_shared_futures_remaining_.fetch_add(
                    1, std::memory_order_release);
            }
        }
    }

    std::unique_lock<std::mutex> lock(invoker->mutex_);
    if (invoker->number_of_prior_shared_futures_remaining_)
    {
        invoker->status_.store(ON_HOLD, std::memory_order_release);
    }
    else
    {
        invoker->status_.store(RUNNING, std::memory_order_release);
        lock.unlock();
        this->invoke(std::forward<InvokerT>(invoker).get());
    }
}

//-----------------------------------------------------------------------------
template <class SharedFutureContainerT>
bool threaded_callback_queue::must_wait(SharedFutureContainerT&& prior_shared_futures)
{
    return std::any_of(
        prior_shared_futures.begin(),
        prior_shared_futures.end(),
        [](const auto& future_item)
        {
            return detail::get_raw_ptr(future_item)->status_.load(std::memory_order_acquire) !=
                   READY;
        });
}

//-----------------------------------------------------------------------------
template <class SharedFutureContainerT>
void threaded_callback_queue::wait(SharedFutureContainerT&& prior_shared_futures)
{
    bool                    must_wait_flag         = false;
    SharedFutureContainerT& prior_shared_futures_r = prior_shared_futures;

    for (const auto& future_item : prior_shared_futures)
    {
        shared_future_base* prior = detail::get_raw_ptr(future_item);
        switch (prior->status_.load(std::memory_order_acquire))
        {
        case RUNNING:
        case ON_HOLD:
        case CONSTRUCTING:
            must_wait_flag = true;
            break;
        case ENQUEUED:
            must_wait_flag |= !this->try_invoke(prior);
            break;
        default:
            break;
        }
    }

    if (!must_wait_flag || !this->must_wait(prior_shared_futures_r))
    {
        return;
    }

    auto empty_lambda = [] {};
    auto invoker_ptr  = invoker_pointer<decltype(empty_lambda)>(
        invoker<decltype(empty_lambda)>::create(std::move(empty_lambda)));
    invoker_ptr->is_high_priority_ = true;
    this->handle_dependent_invoker(prior_shared_futures_r, invoker_ptr);
    invoker_ptr->wait();
}

//-----------------------------------------------------------------------------
template <class ReturnT>
typename threaded_callback_queue::shared_future<ReturnT>::return_lvalue_ref
threaded_callback_queue::get(shared_future_pointer<ReturnT>& future)
{
    this->wait(std::array<shared_future<ReturnT>*, 1>{future.get()});
    return future->get();
}

//-----------------------------------------------------------------------------
template <class ReturnT>
typename threaded_callback_queue::shared_future<ReturnT>::return_const_lvalue_ref
threaded_callback_queue::get(const shared_future_pointer<ReturnT>& future)
{
    this->wait(std::array<const shared_future<ReturnT>*, 1>{future.get()});
    return future->get();
}

//-----------------------------------------------------------------------------
template <class SharedFutureContainerT, class FT, class... ArgsT>
threaded_callback_queue::shared_future_pointer<threaded_callback_queue::invoke_result<FT>>
threaded_callback_queue::push_dependent(
    SharedFutureContainerT&& prior_shared_futures, FT&& f, ArgsT&&... args)
{
    SharedFutureContainerT& prior_shared_futures_r = prior_shared_futures;
    if (!this->must_wait(prior_shared_futures_r))
    {
        return this->push(std::forward<FT>(f), std::forward<ArgsT>(args)...);
    }

    using invoker_pointer_type = invoker_pointer<FT, ArgsT...>;
    auto invoker_ptr           = invoker_pointer_type(
        invoker<FT, ArgsT...>::create(std::forward<FT>(f), std::forward<ArgsT>(args)...));

    this->push(
        &threaded_callback_queue::
            handle_dependent_invoker<SharedFutureContainerT, invoker_pointer_type>,
        this,
        prior_shared_futures_r,
        invoker_ptr);

    return invoker_ptr;
}

//-----------------------------------------------------------------------------
template <class FT, class... ArgsT>
void threaded_callback_queue::push_control(FT&& f, ArgsT&&... args)
{
    struct worker
    {
        void operator()(threaded_callback_queue* self, FT&& _f, ArgsT&&... _args)
        {
            _f(std::forward<ArgsT>(_args)...);
            std::scoped_lock lock(self->control_mutex_);
            self->control_futures_.erase(future_);
        }

        shared_future_base_pointer future_;
    };

    worker w;
    using invoker_pointer_type = invoker_pointer<worker, threaded_callback_queue*, FT, ArgsT...>;
    auto invoker_ptr =
        invoker_pointer_type(invoker<worker, threaded_callback_queue*, FT, ArgsT...>::create(
            w, this, std::forward<FT>(f), std::forward<ArgsT>(args)...));
    w.future_ = invoker_ptr;

    auto local_control_futures = [this, &invoker_ptr]
    {
        std::scoped_lock lock(control_mutex_);
        auto             result = control_futures_;
        control_futures_.emplace(invoker_ptr);
        return result;
    }();

    if (!this->must_wait(local_control_futures))
    {
        if (threads_.empty())
        {
            invoker_ptr->status_.store(RUNNING, std::memory_order_relaxed);
            (*invoker_ptr)();
            return;
        }

        {
            std::scoped_lock invoker_lock(invoker_ptr->mutex_);
            invoker_ptr->status_.store(ENQUEUED, std::memory_order_release);

            std::scoped_lock lock(mutex_);
            invoker_ptr->invoker_index_ =
                invoker_queue_.empty() ? 0 : this->front_invoker_index() - 1;
            invoker_queue_.emplace_front(invoker_ptr);
        }
        condition_variable_.notify_one();
        return;
    }

    invoker_ptr->is_high_priority_ = true;
    this->handle_dependent_invoker(local_control_futures, invoker_ptr);
}

//-----------------------------------------------------------------------------
template <class FT, class... ArgsT>
threaded_callback_queue::shared_future_pointer<threaded_callback_queue::invoke_result<FT>>
threaded_callback_queue::push(FT&& f, ArgsT&&... args)
{
    auto invoker_ptr = invoker_pointer<FT, ArgsT...>(
        invoker<FT, ArgsT...>::create(std::forward<FT>(f), std::forward<ArgsT>(args)...));
    invoker_ptr->status_.store(ENQUEUED, std::memory_order_release);

    {
        std::scoped_lock lock(mutex_);
        invoker_ptr->invoker_index_ = this->back_invoker_index() + 1;
        invoker_queue_.emplace_back(invoker_ptr);
    }

    condition_variable_.notify_one();
    return invoker_ptr;
}
