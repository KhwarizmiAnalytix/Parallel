/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Comprehensive test suite for threaded_callback_queue functionality
 * Tests callback queue operations, threading, and both return value and void specializations
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ParallelTest.h"
#include "tools/threaded_callback_queue.h"

namespace parallel
{
struct IntArray
{
    void        SetName(const std::string& name) { this->name = name; }
    std::string name;
};

//-----------------------------------------------------------------------------
void RunThreads(int nthreadsBegin, int nthreadsEnd)
{
    auto            queue = std::make_unique<threaded_callback_queue>();
    std::atomic_int count(0);
    int             N = 10000;

    // Spamming controls
    for (int i = 0; i < 6; ++i)
    {
        queue->set_number_of_threads(nthreadsBegin);
        queue->set_number_of_threads(nthreadsEnd);
    }

    // We are testing if the queue can properly resize itself and doesn't have deadlocks
    for (int i = 0; i < N; ++i)
    {
        auto array  = std::make_shared<IntArray>();
        auto array2 = std::make_shared<IntArray>();
        queue->push(
            [&count, array, array2](const int& n, const double&&, char)
            {
                array2->SetName(std::to_string(n).c_str());
                array->SetName(std::to_string(n).c_str());
                ++count;
            },
            i,
            0,
            'a');
    }

    // If the jobs are not run, this test will do an infinite loop
    while (count != N)
        ;

    // Checking how the queue behaves when being destroyed.
    queue->set_number_of_threads(nthreadsBegin);
    queue->set_number_of_threads(nthreadsEnd);
}

//=============================================================================
struct A
{
    A() = default;
    A(A&& other) noexcept : array(std::move(other.array)), val(other.val) {}
    A(const A& other) : array(other.array), val(other.val) {}
    void f(A&, A&&) {}
    void const_f(A&, A&&) const {}
    void operator()(A&, A&&) { std::cout << array->name << std::endl; }
    int& get() { return val; }

    std::shared_ptr<IntArray> array = std::make_shared<IntArray>();
    int                       val   = 0;
};

//-----------------------------------------------------------------------------
void f(A&, A&&) {}

//-----------------------------------------------------------------------------
bool TestFunctionTypeCompleteness()
{
    // We create a queue outside of the score where things are pushed to ensure that the pushed
    // objects are persistent.
    auto queue = std::make_unique<threaded_callback_queue>();
    {
        // Testing the queue on some exotic inputs

        // lambdas
        queue->push([] {});  // empty lambda used to fail with MSVC ARM64
        queue->push([](A&&) {}, A());
        queue->push([](A&, const A&, A&&, const A&&) {}, A(), A(), A(), A());

        // member function pointers
        queue->push(&A::f, A(), A(), A());
        queue->push(&A::const_f, A(), A(), A());

        std::shared_ptr<A> persistentA = std::make_shared<A>();

        // Fetching an lvalue reference return type
        auto future = queue->push(&A::get, persistentA);

        // functor
        queue->push(A(), A(), A());

        // function pointer
        queue->push(&f, A(), A());

        // Passing an lvalue reference, which needs to be copied.
        A a;
        queue->push(a, A(), A());

        // Passing a pointer wrapped functor
        queue->push(std::unique_ptr<A>(new A()), A(), A());

        // Passing a pointer wrapped object with a member function pointer
        queue->push(&A::f, std::unique_ptr<A>(new A()), A(), A());

        // Passing a std::function
        std::function<void(A&, A&&)> func = f;
        queue->push(func, A(), A());

        // Testing lvalue reference return type behavior
        int& val = queue->get(future);
        if (&val != &persistentA->val)
        {
            std::cout << "lvalue reference was not correctly passed through the queue."
                      << std::endl;
            return false;
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
bool TestSharedFutures()
{
    int  N      = 10;
    bool retVal = true;
    while (--N && retVal)
    {
        std::atomic_int count(0);
        std::mutex      mutex;
        auto            queue = std::make_unique<threaded_callback_queue>();
        queue->set_number_of_threads(4);

        auto f = [&count, &mutex](std::string& s, int low)
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (count++ < low)
            {
                std::cerr << "Task " << s << " started too early, in " << count.load()
                          << "th position instead of " << (low + 1) << "th.\n";
                return false;
            }
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return true;
        };

        using Array = std::vector<threaded_callback_queue::shared_future_pointer<bool>>;

        int n = 10;

        Array futures;

        auto future1 = queue->push(f, "t1", 0);
        auto future2 = queue->push_dependent(Array{future1}, f, "t2", 1);
        auto future3 = queue->push_dependent(Array{future1, future2}, f, "t3", 2);
        // These pushes makes the scenario where future2 and future4 are ready to run but have a higher
        // future id than them. SharedFuture2 and future4 will need to wait here and we're ensuring
        // everything goes well.
        for (int i = 0; i < n; ++i)
        {
            futures.emplace_back(queue->push(f, "spam", 0));
        }
        auto fastFuture = queue->push(f, "spam", 0);
        auto future4    = queue->push_dependent(Array{future2}, f, "t4", 3);
        auto future5    = queue->push_dependent(Array{future3, future4}, f, "t5", 4);
        auto future6    = queue->push(f, "t6", 0);

        futures.emplace_back(future1);
        futures.emplace_back(future2);
        futures.emplace_back(future3);
        futures.emplace_back(future4);
        futures.emplace_back(future5);
        futures.emplace_back(future6);

        // Testing the case where Wait executes the task associated with a function that wasn't invoked
        // yet.
        queue->wait(Array{fastFuture});

        // Testing all other scenarios in Wait
        queue->wait(futures);

        for (auto& future : futures)
        {
            retVal &= queue->get(future);
        }

        // Join workers before `mutex` / `count` go out of scope. Captured
        // by the pushed lambdas; destroying the queue last used to re-run
        // wait()-completed tasks against a dead mutex.
        queue.reset();
    }

    return retVal;
}

PARALLELTEST(TestThreadedCallbackQueue, Test)
{
    bool retVal = parallel::TestSharedFutures();

    retVal &= parallel::TestFunctionTypeCompleteness();

    parallel::RunThreads(2, 8);

    parallel::RunThreads(8, 2);

    EXPECT_TRUE(retVal);
}
}  // namespace parallel
