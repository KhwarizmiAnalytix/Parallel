# =============================================================================
# XSigma OpenMP
# (Open Multi-Processing) Configuration Module

# This module configures OpenMP for parallel processing support. It detects OpenMP availability and
# sets up the necessary compiler flags and libraries for cross-platform builds.

# Include guard to prevent multiple inclusions
include_guard(GLOBAL)

# OpenMP Support Flag Controls whether OpenMP is enabled for parallel processing. When enabled,
# provides industry-standard parallel programming support with automatic fallback if OpenMP is not
# available on the system.
option(PARALLEL_ENABLE_OPENMP "Enable OpenMP parallel processing support" OFF)
mark_as_advanced(PARALLEL_ENABLE_OPENMP)
# Only proceed if OpenMP is enabled
if(NOT PARALLEL_ENABLE_OPENMP)
  message("OpenMP support is disabled (PARALLEL_ENABLE_OPENMP=OFF)")
  return()
endif()

message(STATUS "Configuring OpenMP support...")

# =============================================================================
# Find OpenMP

# Try to find OpenMP using CMake's built-in FindOpenMP module
find_package(OpenMP QUIET)

if(OpenMP_CXX_FOUND)
  message(STATUS "✅ OpenMP found")
  message(STATUS "   OpenMP Version: ${OpenMP_CXX_VERSION}")
  message(STATUS "   OpenMP Flags: ${OpenMP_CXX_FLAGS}")

  # Add OpenMP libraries to the dependency list OpenMP::OpenMP_CXX is the modern CMake imported
  # target
  if(TARGET OpenMP::OpenMP_CXX)
    list(APPEND PROJECT_DEPENDENCY_LIBS OpenMP::OpenMP_CXX)
    message(STATUS "   OpenMP::OpenMP_CXX target available")
  endif()

  # Set flag to indicate OpenMP is available
  set(PROJECT_OPENMP_FOUND TRUE CACHE BOOL "OpenMP was found successfully" FORCE)

  message(STATUS "OpenMP configuration complete")
else()
  message(STATUS "❌ OpenMP not found on this system")
  message(STATUS "   OpenMP support will be disabled")
  message(STATUS "   The code will fall back to non-OpenMP implementations")

  # Disable OpenMP since it's not available
  set(PARALLEL_ENABLE_OPENMP OFF CACHE BOOL "Enable OpenMP parallel processing support" FORCE)
  set(PROJECT_OPENMP_FOUND FALSE CACHE BOOL "OpenMP was found successfully" FORCE)

  # Provide helpful information for users who want OpenMP
  if(APPLE)
    message(STATUS "")
    message(STATUS "To enable OpenMP on macOS, you can:")
    message(STATUS "  1. Install via Homebrew: brew install libomp")
    message(STATUS "  2. Install via Conda: conda install llvm-openmp")
    message(STATUS "  3. Use a compiler with built-in OpenMP support (e.g., GCC)")
    message(STATUS "")
  elseif(WIN32)
    message(STATUS "")
    message(STATUS "To enable OpenMP on Windows:")
    message(STATUS "  1. Use MSVC compiler (OpenMP included)")
    message(STATUS "  2. Use Intel compiler (OpenMP included)")
    message(STATUS "  3. Install LLVM/Clang with OpenMP support")
    message(STATUS "")
  else()
    message(STATUS "")
    message(STATUS "To enable OpenMP on Linux:")
    message(STATUS "  1. Install via package manager: sudo apt-get install libomp-dev")
    message(STATUS "  2. Use GCC or Clang with OpenMP support")
    message(STATUS "")
  endif()
endif()
