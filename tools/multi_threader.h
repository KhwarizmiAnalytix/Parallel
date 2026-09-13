/**
 * @class   multi_threader
 * @brief   A class for performing multithreaded execution
 *
 * multi_threader is a class that provides support for multithreaded
 * execution using pthreads on POSIX systems, or Win32 threads on
 * Windows.  This class can be used to execute a single
 * method on multiple threads, or to specify a method per thread.
 */

#pragma once

#include <atomic>  // For std::atomic
#include <memory>  // For std::unique_ptr
#include <mutex>   // For std::mutex

#include "common/parallel_export.h"

#ifndef PARALLEL_MAX_THREADS
#define PARALLEL_MAX_THREADS 64
#endif

using thread_function_type = void (*)(void*);

#if PARALLEL_HAS_PTHREADS
#include <pthread.h>
using thread_process_id_type = pthread_t;
using multi_threader_id_type = pthread_t;
#elif PARALLEL_HAS_WIN32_THREADS
#include <windows.h>
typedef HANDLE thread_process_id_type;
typedef DWORD  multi_threader_id_type;
#else
typedef int thread_process_id_type;
typedef int multi_threader_id_type;
#endif

class PARALLEL_VISIBILITY multi_threader
{
public:
    PARALLEL_API static multi_threader* create();
    virtual ~multi_threader();

    /**
   * This is the structure that is passed to the thread that is
   * created from the single_method_execute, multiple_method_execute or
   * the spawn_thread method. It is passed in as a void *, and it is
   * up to the method to cast correctly and extract the information.
   * The thread_id is a number between 0 and number_of_threads-1 that indicates
   * the id of this thread. The number_of_threads is number_of_threads_ for
   * threads created from single_method_execute or multiple_method_execute,
   * and it is 1 for threads created from spawn_thread.
   * The user_data is the (void *)arg passed into the set_single_method,
   * set_multiple_method, or spawn_thread method.
   */
    class thread_info
    {
    public:
        int               thread_id;
        int               number_of_threads;
        std::atomic<int>* active_flag;
        std::mutex*       active_flag_lock;
        void*             user_data;
    };

    ///@{
    /**
   * Get/Set the number of threads to create. It will be clamped to the range
   * 1 - PARALLEL_MAX_THREADS, so the caller of this method should check that the
   * requested number of threads was accepted.
   */
    PARALLEL_API void        set_number_of_threads(int num);
    PARALLEL_API virtual int get_number_of_threads();
    ///@}

    ///@{
    /**
   * Set/Get the maximum number of threads VTK was allocated to support.
   */
    PARALLEL_API static int get_global_static_maximum_number_of_threads();
    ///@}

    ///@{
    /**
   * Set/Get the maximum number of threads to use when multithreading.
   * This limits and overrides any other settings for multithreading.
   * A value of zero indicates no limit.
   */
    PARALLEL_API static void set_global_maximum_number_of_threads(int val);
    PARALLEL_API static int  get_global_maximum_number_of_threads();
    ///@}

    ///@{
    /**
   * Set/Get the value which is used to initialize the number_of_threads
   * in the constructor.  Initially this default is set to the number of
   * processors or PARALLEL_MAX_THREADS (which ever is less).
   */
    PARALLEL_API static void set_global_default_number_of_threads(int val);
    PARALLEL_API static int  get_global_default_number_of_threads();
    ///@}

    // These methods are excluded from wrapping 1) because the
    // wrapper gives up on them and 2) because they really shouldn't be
    // called from a script anyway.

    /**
   * Execute the single_method (as define by set_single_method) using
   * number_of_threads_ threads.
   */
    PARALLEL_API void single_method_execute();

    /**
   * Execute the multiple_methods (as define by calling set_multiple_method
   * for each of the required number_of_threads_ methods) using
   * number_of_threads_ threads.
   */
    PARALLEL_API void multiple_method_execute();

    /**
   * Set the single_method to f() and the user_data field of the
   * thread_info that is passed to it will be data.
   * This method (and all the methods passed to set_multiple_method)
   * must be of type thread_function_type and must take a single argument of
   * type void *.
   */
    PARALLEL_API void set_single_method(thread_function_type f, void* data);

    /**
   * Set the multiple_method at the given index to f() and the user_data
   * field of the thread_info that is passed to it will be data.
   */
    PARALLEL_API void set_multiple_method(int index, thread_function_type f, void* data);

    /**
   * Create a new thread for the given function. Return a thread id
   * which is a number between 0 and PARALLEL_MAX_THREADS - 1. This id should
   * be used to kill the thread at a later time.
   */
    PARALLEL_API int spawn_thread(thread_function_type f, void* data);

    /**
   * Terminate the thread that was created with a spawn_thread_execute()
   */
    PARALLEL_API void terminate_thread(int thread_id);

    /**
   * Determine if a thread is still active
   */
    PARALLEL_API bool is_thread_active(int thread_id);

    /**
   * Get the thread identifier of the calling thread.
   */
    PARALLEL_API static multi_threader_id_type get_current_thread_id();

    /**
   * Check whether two thread identifiers refer to the same thread.
   */
    PARALLEL_API static bool threads_equal(multi_threader_id_type t1, multi_threader_id_type t2);

protected:
    multi_threader();

    // The number of threads to use
    int number_of_threads_;

    // An array of thread info containing a thread id
    // (0, 1, 2, .. PARALLEL_MAX_THREADS-1), the thread count, and a pointer
    // to void so that user data can be passed to each thread
    thread_info thread_info_array_[PARALLEL_MAX_THREADS];

    // The methods
    thread_function_type single_method_;
    thread_function_type multiple_method_[PARALLEL_MAX_THREADS];

    // Storage of MutexFunctions and ints used to control spawned
    // threads and the spawned thread ids
    std::atomic<int>            spawned_thread_active_flag_[PARALLEL_MAX_THREADS];
    std::unique_ptr<std::mutex> spawned_thread_active_flag_lock_[PARALLEL_MAX_THREADS];
    thread_process_id_type      spawned_thread_process_id_[PARALLEL_MAX_THREADS];
    thread_info                 spawned_thread_info_array_[PARALLEL_MAX_THREADS];

    // Internal storage of the data
    void* single_data_;
    void* multiple_data_[PARALLEL_MAX_THREADS];

private:
    multi_threader(const multi_threader&) = delete;
    void operator=(const multi_threader&) = delete;
};

using thread_info_struct = multi_threader::thread_info;
