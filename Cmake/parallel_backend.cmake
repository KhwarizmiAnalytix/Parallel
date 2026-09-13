# =============================================================================
# SMP Backend
# Selection Configuration

# This module handles the selection of the PARALLEL (Symmetric Multi-Processing) backend for
# XSigma. Users can specify which backend to use via: - Command-line: python setup.py config
# --parallel.std (or --parallel.openmp, --parallel.tbb) - CMake directly: cmake
# -DPARALLEL_BACKEND=std (or openmp, tbb)
#
# ARGUMENT FLOW: 1. User runs: python setup.py config --parallel.tbb 2. parse_args() in setup.py
# converts "--parallel.tbb" to "parallel.tbb" 3. XSigmaFlags.__process_arg_list() processes
# "parallel.tbb" and sets: - self.__value["parallel_backend"] = "tbb" - Adds to builder_suffix:
# "_parallel_tbb" 4. XSigmaFlags.create_cmake_flags() generates: -DPARALLEL_BACKEND=tbb 5. CMake
# receives PARALLEL_BACKEND=tbb and this module processes it 6. This module enables/disables
# PARALLEL_ENABLE_TBB and PARALLEL_ENABLE_OPENMP 7. compile_definitions.cmake converts these to
# PARALLEL_HAS_TBB and PARALLEL_HAS_OPENMP
#
# SUPPORTED BACKENDS: - std:     Standard C++ thread backend (std_thread) Result:
# PARALLEL_HAS_TBB=0, PARALLEL_HAS_OPENMP=0 - openmp:  OpenMP backend for parallel processing
# Result: PARALLEL_HAS_OPENMP=1, PARALLEL_HAS_TBB=0 - tbb:     Intel Threading Building Blocks (TBB)
# backend Result: PARALLEL_HAS_TBB=1, PARALLEL_HAS_OPENMP=0
#
# Only one backend can be active at a time. The selected backend controls which implementation is
# compiled and linked into the XSigma Core library.

include_guard(GLOBAL)

# Define the SMP backend option with default value
set(PARALLEL_BACKEND "std" CACHE STRING "SMP backend selection: std, openmp, or tbb")
set_property(CACHE PARALLEL_BACKEND PROPERTY STRINGS std openmp tbb)

# Validate the selected backend
string(TOLOWER "${PARALLEL_BACKEND}" _parallel_backend_lower)
if(NOT _parallel_backend_lower MATCHES "^(std|openmp|tbb)$")
  message(FATAL_ERROR "Invalid PARALLEL_BACKEND value: '${PARALLEL_BACKEND}'. "
                      "Valid options are: std, openmp, or tbb"
  )
endif()

# Configure backend-specific settings based on selection
if(_parallel_backend_lower STREQUAL "std")
  message(STATUS "XSigma: SMP backend set to std_thread (standard C++ threads)")
  # std_thread is always available, no additional configuration needed

elseif(_parallel_backend_lower STREQUAL "openmp")
  message(STATUS "XSigma: SMP backend set to OpenMP")
  # Enable OpenMP support
  set(PARALLEL_ENABLE_OPENMP ON CACHE BOOL "Enable OpenMP parallel processing support" FORCE)
  # Disable TBB if it was enabled
  set(PARALLEL_ENABLE_TBB OFF CACHE BOOL "Enable Intel TBB (Threading Building Blocks) support"
                                    FORCE
  )

elseif(_parallel_backend_lower STREQUAL "tbb")
  message(STATUS "XSigma: SMP backend set to TBB (Intel Threading Building Blocks)")
  # Enable TBB support
  set(PARALLEL_ENABLE_TBB ON CACHE BOOL "Enable Intel TBB (Threading Building Blocks) support"
                                   FORCE
  )
  # Disable OpenMP if it was enabled
  set(PARALLEL_ENABLE_OPENMP OFF CACHE BOOL "Enable OpenMP parallel processing support" FORCE)
endif()

message(STATUS "XSigma: SMP backend configuration complete")
