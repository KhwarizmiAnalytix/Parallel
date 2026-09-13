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

#include "multi_threader.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Undefine min/max macros from Windows headers if they were defined
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

#include <algorithm>

// Need to define "extern_c_thread_function_type" to avoid warning on some
// platforms about passing function pointer to an argument expecting an
// extern "C" function.  Placing the typedef of the function pointer type
// inside an extern "C" block solves this problem.
#if PARALLEL_HAS_PTHREADS
#include <pthread.h>
extern "C"
{
    using extern_c_thread_function_type = void* (*)(void*);
}
#else
using extern_c_thread_function_type = thread_function_type;
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

// Initialize static member that controls global maximum number of threads
static int g_multi_threader_global_maximum_number_of_threads = 0;

void multi_threader::set_global_maximum_number_of_threads(int val)
{
    if (val == g_multi_threader_global_maximum_number_of_threads)
    {
        return;
    }
    g_multi_threader_global_maximum_number_of_threads = val;
}

int multi_threader::get_global_maximum_number_of_threads()
{
    return g_multi_threader_global_maximum_number_of_threads;
}

int multi_threader::get_global_static_maximum_number_of_threads()
{
    return PARALLEL_MAX_THREADS;
}

// 0 => Not initialized.
static int g_multi_threader_global_default_number_of_threads = 0;

void multi_threader::set_global_default_number_of_threads(int val)
{
    if (val == g_multi_threader_global_default_number_of_threads)
    {
        return;
    }
    g_multi_threader_global_default_number_of_threads = val;
}

int multi_threader::get_global_default_number_of_threads()
{
    if (g_multi_threader_global_default_number_of_threads == 0)
    {
        int num = 1;  // default is 1

#if PARALLEL_HAS_PTHREADS
        // Default the number of threads to be the number of available
        // processors if we are using pthreads()
#ifdef _SC_NPROCESSORS_ONLN
        num = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(_SC_NPROC_ONLN)
        num = sysconf(_SC_NPROC_ONLN);
#endif
#endif

#ifdef __APPLE__
        // Determine the number of CPU cores.
        // hw.logicalcpu takes into account cores/CPUs that are
        // disabled because of power management.
        size_t    dataLen = sizeof(int);  // 'num' is an 'int'
        const int result  = sysctlbyname("hw.logicalcpu", &num, &dataLen, nullptr, 0);
        if (result == -1)
        {
            num = 1;
        }
#endif

#ifdef _WIN32
        {
            SYSTEM_INFO sysInfo;                 // NOLINT
            GetSystemInfo(&sysInfo);             // NOLINT
            num = sysInfo.dwNumberOfProcessors;  // NOLINT
        }
#endif

#if !PARALLEL_HAS_WIN32_THREADS
#if !PARALLEL_HAS_PTHREADS
        // If we are not multithreading, the number of threads should
        // always be 1
        // cppcheck-suppress redundantAssignment
        num = 1;
#endif
#endif

        // Lets limit the number of threads to PARALLEL_MAX_THREADS
        num = std::min(num, PARALLEL_MAX_THREADS);

        g_multi_threader_global_default_number_of_threads = num;
    }

    return g_multi_threader_global_default_number_of_threads;
}

// Constructor. Default all the methods to nullptr. Since the
// thread_info_array is static, the thread_ids can be initialized here
// and will not change.
multi_threader::multi_threader()
{
    for (int i = 0; i < PARALLEL_MAX_THREADS; i++)
    {
        thread_info_array_[i].thread_id        = i;
        thread_info_array_[i].active_flag      = nullptr;
        thread_info_array_[i].active_flag_lock = nullptr;
        multiple_method_[i]                    = nullptr;
        spawned_thread_active_flag_[i]         = 0;
        // Allocated eagerly (rather than lazily in spawn_thread()) so that
        // slot reuse never races two threads creating the same mutex.
        spawned_thread_active_flag_lock_[i]     = std::make_unique<std::mutex>();
        spawned_thread_info_array_[i].thread_id = i;
        spawned_thread_process_id_[i]           = thread_process_id_type();
        multiple_data_[i]                       = nullptr;
    }

    single_method_     = nullptr;
    single_data_       = nullptr;
    number_of_threads_ = multi_threader::get_global_default_number_of_threads();
}

multi_threader::~multi_threader()
{
    for (auto& thread_info : thread_info_array_)
    {
        delete thread_info.active_flag_lock;
        thread_info.active_flag_lock = nullptr;

        delete thread_info.active_flag;
        thread_info.active_flag = nullptr;
    }
}

multi_threader* multi_threader::create()
{
    return new multi_threader();
}

//------------------------------------------------------------------------------
int multi_threader::get_number_of_threads()
{
    int num = number_of_threads_;
    if (g_multi_threader_global_maximum_number_of_threads > 0 &&
        num > g_multi_threader_global_maximum_number_of_threads)
    {
        num = g_multi_threader_global_maximum_number_of_threads;
    }
    return num;
}

void multi_threader::set_number_of_threads(int num)
{
    num                = std::max(num, 1);
    num                = std::min(num, PARALLEL_MAX_THREADS);
    number_of_threads_ = num;
}

// Set the user defined method that will be run on number_of_threads threads
// when single_method_execute is called.
void multi_threader::set_single_method(thread_function_type f, void* data)
{
    single_method_ = f;
    single_data_   = data;
}

// Set one of the user defined methods that will be run on number_of_threads
// threads when multiple_method_execute is called. This method should be
// called with index = 0, 1, ..,  number_of_threads-1 to set up all the
// required user defined methods
void multi_threader::set_multiple_method(int index, thread_function_type f, void* data)
{
    // You can only set the method for 0 through number_of_threads-1
    if (index >= number_of_threads_)
    {
        // Error: Can't set method with index >= number_of_threads
        return;
    }

    multiple_method_[index] = f;
    multiple_data_[index]   = data;
}

// Execute the method set as the single_method on number_of_threads threads.
void multi_threader::single_method_execute()
{
    int thread_loop = 0;

#if PARALLEL_HAS_WIN32_THREADS
    DWORD  threadId;
    HANDLE process_id[PARALLEL_MAX_THREADS] = {};  // NOLINT(misc-const-correctness)
#endif

#if PARALLEL_HAS_PTHREADS
    pthread_t process_id[PARALLEL_MAX_THREADS] = {};  // NOLINT(misc-const-correctness)
#endif

    if (single_method_ == nullptr)
    {
        // Error: No single method set!
        return;
    }

    // obey the global maximum number of threads limit
    if ((g_multi_threader_global_maximum_number_of_threads != 0) &&
        number_of_threads_ > g_multi_threader_global_maximum_number_of_threads)
    {
        number_of_threads_ = g_multi_threader_global_maximum_number_of_threads;
    }

#if PARALLEL_HAS_WIN32_THREADS
    // Using CreateThread on Windows
    //
    // We want to use CreateThread to start number_of_threads_ - 1
    // additional threads which will be used to call single_method_().
    // The parent thread will also call this routine.  When it is done,
    // it will wait for all the children to finish.
    //
    // First, start up the number_of_threads_-1 processes.  Keep track
    // of their process ids for use later in the waitid call
    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        thread_info_array_[thread_loop].user_data         = single_data_;
        thread_info_array_[thread_loop].number_of_threads = number_of_threads_;
        process_id[thread_loop]                           = CreateThread(  // NOLINT
            nullptr,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(
                single_method_),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            static_cast<void*>(&thread_info_array_[thread_loop]),
            0,
            &threadId);
        if (process_id[thread_loop] == nullptr)
        {
            // Error in thread creation !!!
        }
    }

    // Now, the parent thread calls single_method_() itself
    thread_info_array_[0].user_data         = single_data_;
    thread_info_array_[0].number_of_threads = number_of_threads_;
    single_method_(static_cast<void*>(&thread_info_array_[0]));

    // The parent thread has finished single_method_() - so now it
    // waits for each of the other processes to exit
    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        WaitForSingleObject(process_id[thread_loop], INFINITE);  // NOLINT
    }

    // close the threads
    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        CloseHandle(process_id[thread_loop]);  // NOLINT
    }
#endif

#if PARALLEL_HAS_PTHREADS
    // Using POSIX threads
    //
    // We want to use pthread_create to start number_of_threads_-1 additional
    // threads which will be used to call single_method_(). The
    // parent thread will also call this routine.  When it is done,
    // it will wait for all the children to finish.
    //
    // First, start up the number_of_threads_-1 processes.  Keep track
    // of their process ids for use later in the pthread_join call

    pthread_attr_t attr;

    pthread_attr_init(&attr);
#if !defined(__CYGWIN__) && !defined(__EMSCRIPTEN__)
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);
#endif

    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        thread_info_array_[thread_loop].user_data         = single_data_;
        thread_info_array_[thread_loop].number_of_threads = number_of_threads_;

        const int threadError = pthread_create(
            &(process_id[thread_loop]),
            &attr,
            reinterpret_cast<extern_c_thread_function_type>(single_method_),
            static_cast<void*>(&thread_info_array_[thread_loop]));
        if (threadError != 0)
        {
            // Error: Unable to create a thread
        }
    }

    // Now, the parent thread calls single_method_() itself
    thread_info_array_[0].user_data         = single_data_;
    thread_info_array_[0].number_of_threads = number_of_threads_;
    single_method_(static_cast<void*>(&thread_info_array_[0]));

    // The parent thread has finished single_method_() - so now it
    // waits for each of the other processes to exit
    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        pthread_join(process_id[thread_loop], nullptr);
    }
#endif

#if !PARALLEL_HAS_WIN32_THREADS
#if !PARALLEL_HAS_PTHREADS
    (void)thread_loop;
    // There is no multi threading, so there is only one thread.
    thread_info_array_[0].user_data         = single_data_;
    thread_info_array_[0].number_of_threads = number_of_threads_;
    single_method_(static_cast<void*>(&thread_info_array_[0]));
#endif
#endif
}

void multi_threader::multiple_method_execute()
{
    int thread_loop;

#if PARALLEL_HAS_WIN32_THREADS
    DWORD  threadId;
    HANDLE process_id[PARALLEL_MAX_THREADS] = {};  // NOLINT(misc-const-correctness)
#endif

#if PARALLEL_HAS_PTHREADS
    pthread_t process_id[PARALLEL_MAX_THREADS] = {};  // NOLINT(misc-const-correctness)
#endif

    // obey the global maximum number of threads limit
    if ((g_multi_threader_global_maximum_number_of_threads != 0) &&
        number_of_threads_ > g_multi_threader_global_maximum_number_of_threads)
    {
        number_of_threads_ = g_multi_threader_global_maximum_number_of_threads;
    }

    for (thread_loop = 0; thread_loop < number_of_threads_; thread_loop++)
    {
        if (multiple_method_[thread_loop] == static_cast<thread_function_type>(nullptr))
        {
            // Error: No multiple method set
            return;
        }
    }

#if PARALLEL_HAS_WIN32_THREADS
    // Using CreateThread on Windows
    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        thread_info_array_[thread_loop].user_data         = multiple_data_[thread_loop];
        thread_info_array_[thread_loop].number_of_threads = number_of_threads_;
        process_id[thread_loop]                           = CreateThread(  // NOLINT
            nullptr,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(
                multiple_method_
                    [thread_loop]),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            static_cast<void*>(&thread_info_array_[thread_loop]),
            0,
            &threadId);
        if (process_id[thread_loop] == nullptr)
        {
            // Error in thread creation !!!
        }
    }

    // Now, the parent thread calls the last method itself
    thread_info_array_[0].user_data         = multiple_data_[0];
    thread_info_array_[0].number_of_threads = number_of_threads_;
    (multiple_method_[0])(static_cast<void*>(&thread_info_array_[0]));

    // The parent thread has finished its method - so now it
    // waits for each of the other threads to exit
    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        WaitForSingleObject(process_id[thread_loop], INFINITE);  // NOLINT
    }

    // close the threads
    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        CloseHandle(process_id[thread_loop]);  // NOLINT
    }
#endif

#if PARALLEL_HAS_PTHREADS
    // Using POSIX threads
    pthread_attr_t attr;

    pthread_attr_init(&attr);
#if !defined(__CYGWIN__) && !defined(__EMSCRIPTEN__)
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);
#endif

    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        thread_info_array_[thread_loop].user_data         = multiple_data_[thread_loop];
        thread_info_array_[thread_loop].number_of_threads = number_of_threads_;
        pthread_create(
            &(process_id[thread_loop]),
            &attr,
            reinterpret_cast<extern_c_thread_function_type>(multiple_method_[thread_loop]),
            static_cast<void*>(&thread_info_array_[thread_loop]));
    }

    // Now, the parent thread calls the last method itself
    thread_info_array_[0].user_data         = multiple_data_[0];
    thread_info_array_[0].number_of_threads = number_of_threads_;
    (multiple_method_[0])(static_cast<void*>(&thread_info_array_[0]));

    // The parent thread has finished its method - so now it
    // waits for each of the other processes to exit
    for (thread_loop = 1; thread_loop < number_of_threads_; thread_loop++)
    {
        pthread_join(process_id[thread_loop], nullptr);
    }
#endif

#if !PARALLEL_HAS_WIN32_THREADS
#if !PARALLEL_HAS_PTHREADS
    // There is no multi threading, so there is only one thread.
    thread_info_array_[0].user_data         = multiple_data_[0];
    thread_info_array_[0].number_of_threads = number_of_threads_;
    (multiple_method_[0])(static_cast<void*>(&thread_info_array_[0]));
#endif
#endif
}

int multi_threader::spawn_thread(thread_function_type f, void* userdata)
{
    int id;

    for (id = 0; id < PARALLEL_MAX_THREADS; id++)
    {
        const std::scoped_lock lockGuard(*spawned_thread_active_flag_lock_[id]);
        if (spawned_thread_active_flag_[id] == 0)
        {
            // We've got a usable thread id, so grab it
            spawned_thread_active_flag_[id] = 1;
            break;
        }
    }

    if (id >= PARALLEL_MAX_THREADS)
    {
        // Error: You have too many active threads!
        return -1;
    }

    spawned_thread_info_array_[id].user_data         = userdata;
    spawned_thread_info_array_[id].number_of_threads = 1;
    spawned_thread_info_array_[id].active_flag       = &spawned_thread_active_flag_[id];
    spawned_thread_info_array_[id].active_flag_lock  = spawned_thread_active_flag_lock_[id].get();

#if PARALLEL_HAS_WIN32_THREADS
    // Using CreateThread on Windows
    //
    DWORD threadId;                   // NOLINT
    spawned_thread_process_id_[id] =  // NOLINT
        CreateThread(
            nullptr,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(
                f),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            static_cast<void*>(&spawned_thread_info_array_[id]),
            0,
            &threadId);  // NOLINT
    if (spawned_thread_process_id_[id] == nullptr)
    {
        // Error in thread creation !!!
    }
#endif

#if PARALLEL_HAS_PTHREADS
    // Using POSIX threads
    //
    pthread_attr_t attr;
    pthread_attr_init(&attr);
#if !defined(__CYGWIN__) && !defined(__EMSCRIPTEN__)
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);
#endif

    pthread_create(
        &(spawned_thread_process_id_[id]),
        &attr,
        reinterpret_cast<extern_c_thread_function_type>(f),
        static_cast<void*>(&spawned_thread_info_array_[id]));

#endif

#if !PARALLEL_HAS_WIN32_THREADS
#if !PARALLEL_HAS_PTHREADS
    (void)f;
    // There is no multi threading, so there is only one thread.
    // This won't work - so give an error message. Release the slot claimed
    // above instead of destroying its mutex: the lock array is allocated
    // once for the object's lifetime and must stay valid for reuse.
    {
        const std::scoped_lock lockGuard(*spawned_thread_active_flag_lock_[id]);
        spawned_thread_active_flag_[id] = 0;
    }
    id = -1;
#endif
#endif

    return id;
}

void multi_threader::terminate_thread(int thread_id)
{
    // check if the thread_id argument is in range
    if (thread_id >= PARALLEL_MAX_THREADS)
    {
        // Error: thread_id is out of range
        return;
    }

    // If we don't have a lock, then this thread is definitely not active
    if (spawned_thread_active_flag_[thread_id] == 0)
    {
        return;
    }

    // If we do have a lock, use it and find out the status of the active flag
    int val = 0;
    {
        const std::scoped_lock lockGuard(*spawned_thread_active_flag_lock_[thread_id]);
        val = spawned_thread_active_flag_[thread_id];
    }

    // If the active flag is 0, return since this thread is not active
    if (val == 0)
    {
        return;
    }

    // OK - now we know we have an active thread - set the active flag to 0
    // to indicate to the thread that it should terminate itself
    {
        const std::scoped_lock lockGuard(*spawned_thread_active_flag_lock_[thread_id]);
        spawned_thread_active_flag_[thread_id].store(0, std::memory_order_release);
    }

#if PARALLEL_HAS_WIN32_THREADS
    WaitForSingleObject(spawned_thread_process_id_[thread_id], INFINITE);  // NOLINT
    CloseHandle(spawned_thread_process_id_[thread_id]);                    // NOLINT
#endif

#if PARALLEL_HAS_PTHREADS
    pthread_join(spawned_thread_process_id_[thread_id], nullptr);
#endif

#if !PARALLEL_HAS_WIN32_THREADS
#if !PARALLEL_HAS_PTHREADS
    // There is no multi threading, so there is only one thread.
    // This won't work - so give an error message.
#endif
#endif
}

//------------------------------------------------------------------------------
multi_threader_id_type multi_threader::get_current_thread_id()
{
#if PARALLEL_HAS_PTHREADS
    return pthread_self();
#elif PARALLEL_HAS_WIN32_THREADS
    return GetCurrentThreadId();  // NOLINT
#else
    // No threading implementation.  Assume all callers are in the same
    // thread.
    return 0;
#endif
}

bool multi_threader::is_thread_active(int thread_id)
{
    // check if the thread_id argument is in range
    if (thread_id >= PARALLEL_MAX_THREADS)
    {
        // Error: thread_id is out of range
        return false;
    }

    // The per-slot lock is allocated once for the object's lifetime; use it
    // to get the active flag value.
    int val = 0;
    {
        const std::scoped_lock lockGuard(*spawned_thread_active_flag_lock_[thread_id]);
        val = spawned_thread_active_flag_[thread_id];
    }

    // now return that value
    return val != 0;
}

//------------------------------------------------------------------------------
bool multi_threader::threads_equal(multi_threader_id_type t1, multi_threader_id_type t2)
{
#if PARALLEL_HAS_PTHREADS
    return pthread_equal(t1, t2) != 0;
#elif PARALLEL_HAS_WIN32_THREADS
    return t1 == t2;
#else
    (void)t1;
    (void)t2;
    // No threading implementation.  Assume all callers are in the same
    // thread.
    return true;
#endif
}
