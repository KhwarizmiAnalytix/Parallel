# =============================================================================
# Parallel Threading
# Library Detection and Configuration This module detects the threading library available on the
# platform and sets appropriate CMake variables and compiler definitions: -
# PARALLEL_ENABLE_PTHREADS: ON on Unix-like systems with pthreads support -
# PARALLEL_ENABLE_WIN32_THREADS: ON on Windows systems - PARALLEL_MAX_THREADS: Maximum number of
# threads supported (default: 64)
#
# Compile definitions emitted (always 1 or 0, never ON/OFF): - PARALLEL_HAS_PTHREADS=1/0 -
# PARALLEL_HAS_WIN32_THREADS=1/0
#
# The module also ensures the appropriate thread library is linked to targets.

cmake_minimum_required(VERSION 3.16)

# Include guard to prevent multiple inclusions
include_guard(GLOBAL)

message(STATUS "Parallel: Detecting threading library support...")

# =============================================================================
# Thread Library
# Detection

# Use CMake's built-in thread detection
find_package(Threads REQUIRED)

# =============================================================================
# Platform-Specific
# Thread Configuration

# Initialize thread flags to OFF
set(PARALLEL_ENABLE_PTHREADS OFF CACHE INTERNAL "Use POSIX threads (pthreads)")
set(PARALLEL_ENABLE_WIN32_THREADS OFF CACHE INTERNAL "Use Win32 threads")

# Detect threading model based on platform and CMake's thread detection
if(WIN32)
  # Windows platform uses Win32 threads
  set(PARALLEL_ENABLE_WIN32_THREADS ON CACHE INTERNAL "Use Win32 threads" FORCE)
  set(PARALLEL_ENABLE_PTHREADS OFF CACHE INTERNAL "Use POSIX threads (pthreads)" FORCE)
  message(STATUS "Parallel: Threading model: Win32 threads")

elseif(CMAKE_USE_PTHREADS_INIT OR CMAKE_THREAD_LIBS_INIT MATCHES "pthread")
  # Unix-like systems (Linux, macOS, BSD, etc.) with pthreads
  set(PARALLEL_ENABLE_PTHREADS ON CACHE INTERNAL "Use POSIX threads (pthreads)" FORCE)
  set(PARALLEL_ENABLE_WIN32_THREADS OFF CACHE INTERNAL "Use Win32 threads" FORCE)
  message(STATUS "Parallel: Threading model: POSIX threads (pthreads)")

else()
  # Fallback: no threading support detected
  message(WARNING "Parallel: No threading library detected. Multi-threading will be disabled.")
  set(PARALLEL_ENABLE_PTHREADS OFF CACHE INTERNAL "Use POSIX threads (pthreads)" FORCE)
  set(PARALLEL_ENABLE_WIN32_THREADS OFF CACHE INTERNAL "Use Win32 threads" FORCE)
endif()

# =============================================================================
# Maximum Thread Count
# Configuration

# Set maximum number of threads (can be overridden by user)
if(NOT DEFINED PARALLEL_MAX_THREADS)
  set(PARALLEL_MAX_THREADS 64 CACHE STRING "Maximum number of threads supported by Parallel")
endif()

# Validate PARALLEL_MAX_THREADS
if(PARALLEL_MAX_THREADS LESS 1)
  message(
    FATAL_ERROR "Parallel: PARALLEL_MAX_THREADS must be at least 1 (got ${PARALLEL_MAX_THREADS})"
  )
endif()

message(STATUS "Parallel: Maximum threads: ${PARALLEL_MAX_THREADS}")

# =============================================================================
# Thread Library
# Linking

# Create an interface library for threading support
if(NOT TARGET Threads::threads)
  add_library(parallel_threads INTERFACE)
  add_library(Threads::threads ALIAS parallel_threads)

  # Link the appropriate thread library
  if(PARALLEL_ENABLE_PTHREADS)
    # Link pthreads library on Unix-like systems
    target_link_libraries(parallel_threads INTERFACE Threads::Threads)

    # Add pthread compile flags if needed (for some compilers)
    if(CMAKE_THREAD_LIBS_INIT MATCHES "-pthread")
      target_compile_options(parallel_threads INTERFACE "-pthread")
    endif()

  elseif(PARALLEL_ENABLE_WIN32_THREADS)
    # Win32 threads are part of the Windows API, no additional linking needed But we ensure
    # Threads::Threads is available for consistency
    target_link_libraries(parallel_threads INTERFACE Threads::Threads)
  endif()

  # Export thread configuration as compile definitions (1/0) on the interface target. $<BOOL:...>
  # converts ON/OFF → 1/0 so the preprocessor always sees an integer.
  target_compile_definitions(
    parallel_threads
    INTERFACE PARALLEL_HAS_PTHREADS=$<BOOL:${PARALLEL_ENABLE_PTHREADS}>
              PARALLEL_HAS_WIN32_THREADS=$<BOOL:${PARALLEL_ENABLE_WIN32_THREADS}>
              PARALLEL_MAX_THREADS=${PARALLEL_MAX_THREADS}
  )

  message(STATUS "Parallel: Created Threads::threads interface library")

  # Add to PROJECT_DEPENDENCY_LIBS for automatic linking to Core and other libraries This ensures
  # all Parallel libraries have access to threading support
  get_property(_temp_libs GLOBAL PROPERTY _PROJECT_DEPENDENCY_LIBS)
  list(APPEND _temp_libs Threads::threads)
  set_property(GLOBAL PROPERTY _PROJECT_DEPENDENCY_LIBS "${_temp_libs}")
  message(STATUS "Parallel: Threads::threads added to PROJECT_DEPENDENCY_LIBS")
endif()

# =============================================================================
# Summary

message(STATUS "Parallel: Thread configuration summary:")
message(STATUS "  - PARALLEL_ENABLE_PTHREADS:      ${PARALLEL_ENABLE_PTHREADS}")
message(STATUS "  - PARALLEL_ENABLE_WIN32_THREADS: ${PARALLEL_ENABLE_WIN32_THREADS}")
message(STATUS "  - PARALLEL_MAX_THREADS:          ${PARALLEL_MAX_THREADS}")
message(STATUS "  - CMAKE_THREAD_LIBS_INIT:        ${CMAKE_THREAD_LIBS_INIT}")
