# =============================================================================
# Parallel —
# CMakeLists.txt
#
# Thread-pool, task-queue, multi-threader facade, and pluggable SMP backends (std::thread, OpenMP,
# Intel TBB).
#
# Public compile definitions: PARALLEL_HAS_TBB=1/0           — Intel TBB work-stealing backend
# active PARALLEL_HAS_OPENMP=1/0        — OpenMP directive-based backend active
# PARALLEL_HAS_PTHREADS=1/0      — POSIX threads available PARALLEL_HAS_WIN32_THREADS=1/0 — Win32
# native threading API used
# =============================================================================
cmake_minimum_required(VERSION 3.24)

# Standalone CMake package — any C++ project can consume it via add_subdirectory;
# XSigma is one consumer, not a required host.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(
    Parallel
    VERSION 1.0.0
    DESCRIPTION "Thread pools, task queues, and pluggable SMP backends (std::thread, OpenMP, TBB)"
    HOMEPAGE_URL "https://github.com/KhwarizmiAnalytix/Parallel"
    LANGUAGES CXX C
  )
  set(PARALLEL_STANDALONE ON)
  option(BUILD_SHARED_LIBS "Build shared libraries" ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
else()
  set(PARALLEL_STANDALONE OFF)
endif()

# Own CMake modules: toolchain helpers (lto/cache/sanitize/...) and Parallel-owned
# finders (threads/parallel_backend/openmp/tbb_multithreading) all live in Cmake/.
set(PARALLEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PARALLEL_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# --- Section 1: Feature flags (PARALLEL_ENABLE_*) --- TBB/OpenMP/thread flags are set by
# module-owned finders in Section 3.

# Link-Time Optimization — disabled when PARALLEL_ENABLE_COVERAGE or PARALLEL_ENABLE_SANITIZER is ON
# (LTO conflicts with instrumentation / sanitizers).
include("${PARALLEL_SOURCE_DIR}/Cmake/lto.cmake")
parallel_lto_compute_default(_lto_default PROFILE PERFORMANCE)
set(PARALLEL_LTO_MODE "${_lto_default}"
    CACHE STRING "LTO mode for Parallel: off | thin | full | ipo | auto"
)
set_property(CACHE PARALLEL_LTO_MODE PROPERTY STRINGS off thin full ipo auto)

# Coverage instrumentation — activates -fprofile-instr-generate / --coverage compiler flags for
# this module only. LTO is automatically skipped when coverage is enabled.
option(PARALLEL_ENABLE_COVERAGE "Enable coverage instrumentation for Parallel tests" OFF)

# Testing — gates add_subdirectory(Testing/Cxx).
set(_parallel_testing_default ${PARALLEL_STANDALONE})
if(DEFINED BUILD_TESTING)
  set(_parallel_testing_default ${BUILD_TESTING})
endif()
option(PARALLEL_ENABLE_TESTING "Build Parallel test suite" ${_parallel_testing_default})
# Respect the host's global veto, including on reconfigure.
if(DEFINED BUILD_TESTING AND NOT BUILD_TESTING)
  set(PARALLEL_ENABLE_TESTING OFF)
endif()

# Examples — gates add_subdirectory(Examples) when Parallel examples exist.
option(PARALLEL_ENABLE_EXAMPLES "Build Parallel example programs" OFF)

# Google Test integration for Parallel tests.
option(PARALLEL_ENABLE_GTEST "Enable GoogleTest for Parallel" ON)

# Google Benchmark integration for SMP micro-benchmarks.
option(PARALLEL_ENABLE_BENCHMARK "Enable Google Benchmark for Parallel" ON)

# Icecream distributed compilation launcher (rarely needed; hidden by default).
option(PARALLEL_ENABLE_ICECC "Use Icecream (icecc) distributed compiler for Parallel" OFF)
mark_as_advanced(PARALLEL_ENABLE_ICECC)

# Per-target compiler cache — wraps every compile command with ccache / sccache etc. to speed up
# incremental and CI rebuilds. ON by default.
option(PARALLEL_ENABLE_CACHE "Enable compiler caching for the Parallel target" ON)
mark_as_advanced(PARALLEL_ENABLE_CACHE)

option(PARALLEL_ENABLE_CLANGTIDY "Enable clang-tidy static analysis for Parallel" OFF)
mark_as_advanced(PARALLEL_ENABLE_CLANGTIDY)
option(PARALLEL_ENABLE_FIX "Enable clang-tidy fix-errors and fix options for Parallel" OFF)
mark_as_advanced(PARALLEL_ENABLE_FIX)
option(PARALLEL_ENABLE_IWYU "Enable include-what-you-use analysis for Parallel" OFF)
mark_as_advanced(PARALLEL_ENABLE_IWYU)
option(PARALLEL_ENABLE_SANITIZER "Build Parallel with sanitizer support (Clang/GCC only)" OFF)
set(PARALLEL_SANITIZER_TYPE "address"
    CACHE STRING "Sanitizer type for Parallel. Options: address, undefined, thread, memory, leak"
)
set_property(CACHE PARALLEL_SANITIZER_TYPE PROPERTY STRINGS address undefined thread memory leak)
mark_as_advanced(PARALLEL_ENABLE_SANITIZER PARALLEL_SANITIZER_TYPE)
option(PARALLEL_ENABLE_SPELL "Enable check-only spell checking for Parallel" OFF)
mark_as_advanced(PARALLEL_ENABLE_SPELL)
option(PARALLEL_ENABLE_VALGRIND "Execute Parallel test suite with Valgrind" OFF)
mark_as_advanced(PARALLEL_ENABLE_VALGRIND)
set(PARALLEL_LINKER_CHOICE "default"
    CACHE STRING "Linker for Parallel: default, lld, mold, gold, lld-link"
)
set_property(CACHE PARALLEL_LINKER_CHOICE PROPERTY STRINGS default lld mold gold lld-link)
mark_as_advanced(PARALLEL_LINKER_CHOICE)
set(PARALLEL_CACHE_BACKEND "none"
    CACHE STRING "Compiler cache backend for Parallel. Options: none, ccache, sccache, buildcache"
)
set_property(CACHE PARALLEL_CACHE_BACKEND PROPERTY STRINGS none ccache sccache buildcache)
mark_as_advanced(PARALLEL_CACHE_BACKEND)

# C++ standard — applied to every TU in this module via CMAKE_CXX_STANDARD.
set(PARALLEL_CXX_STANDARD "20" CACHE STRING "C++ standard for Parallel. Options: 17 20 23")
set_property(CACHE PARALLEL_CXX_STANDARD PROPERTY STRINGS 17 20 23)
if(NOT PARALLEL_CXX_STANDARD MATCHES "^(17|20|23)$")
  message(FATAL_ERROR "PARALLEL_CXX_STANDARD must be one of: 17 20 23")
endif()

# --- Section 2: PARALLEL_ENABLE_* → PARALLEL_HAS_* compile definitions --- All ENABLE flags come
# from Section 3 finders; this block is completed after those includes.
set(PARALLEL_DEPENDENCY_COMPILE_DEFINITIONS ${PROJECT_DEPENDENCY_COMPILE_DEFINITIONS})

# --- Section 3: CMake module includes --- Order: introspection → toolchain helpers → Parallel-owned
# finders → coverage/sanitizer → LTO/linker.

# a) Standard CMake introspection
include(CheckCXXSourceCompiles)
include(CheckCXXSourceRuns)
include(CheckSymbolExists)

# b) Shared toolchain helpers
set(CMAKE_CXX_STANDARD ${PARALLEL_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include("${PARALLEL_SOURCE_DIR}/Cmake/checks.cmake") # Compiler and platform requirements
include("${PARALLEL_SOURCE_DIR}/Cmake/build_type.cmake") # Validates / normalises CMAKE_BUILD_TYPE
include("${PARALLEL_SOURCE_DIR}/Cmake/platform.cmake"
)# Platform flags (macOS rpath, Windows /utf-8, …)
include(CMakeDependentOption)

include("${PARALLEL_SOURCE_DIR}/Cmake/cache.cmake") # Configures per-target compiler cache launcher

# c) Parallel-owned feature finders (Library/Parallel/Cmake/)
include("${PARALLEL_SOURCE_DIR}/Cmake/threads.cmake"
)# Detects pthreads / Win32 threads; sets PARALLEL_ENABLE_PTHREADS
include("${PARALLEL_SOURCE_DIR}/Cmake/parallel_backend.cmake"
)# Validates PARALLEL_BACKEND; derives PARALLEL_ENABLE_TBB/OPENMP
include("${PARALLEL_SOURCE_DIR}/Cmake/openmp.cmake"
)# Finds/configures OpenMP when PARALLEL_ENABLE_OPENMP=ON
include("${PARALLEL_SOURCE_DIR}/Cmake/tbb_multithreading.cmake"
)# Finds/builds Intel TBB when PARALLEL_ENABLE_TBB=ON
if(PARALLEL_ENABLE_SANITIZER)
  include("${PARALLEL_SOURCE_DIR}/Cmake/sanitize.cmake")
endif()
if(PARALLEL_ENABLE_SPELL)
  include("${PARALLEL_SOURCE_DIR}/Cmake/spell.cmake")
endif()
if(PARALLEL_ENABLE_VALGRIND)
  include("${PARALLEL_SOURCE_DIR}/Cmake/valgrind.cmake")
endif()
include("${PARALLEL_SOURCE_DIR}/Cmake/helper_macros.cmake")

if(PARALLEL_STANDALONE)
  include(CTest)
endif()
if(PARALLEL_ENABLE_TESTING)
  enable_testing()
endif()

# d) Target-scoped coverage; LTO is skipped when instrumentation is enabled.
if(PARALLEL_ENABLE_COVERAGE)
  include("${PARALLEL_SOURCE_DIR}/Cmake/coverage.cmake")
elseif(MSVC)
  string(APPEND CMAKE_EXE_LINKER_FLAGS " /DEBUG:FULL")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " /DEBUG:FULL")
endif()

# --- Section 2 (continued): detection-based PARALLEL_HAS_* compile definitions ---

# Intel TBB — set by include("${PARALLEL_SOURCE_DIR}/Cmake/tbb_multithreading.cmake").
if(PARALLEL_ENABLE_TBB)
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_TBB=1)
else()
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_TBB=0)
endif()

# OpenMP — set by include("${PARALLEL_SOURCE_DIR}/Cmake/openmp.cmake").
if(PARALLEL_ENABLE_OPENMP)
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_OPENMP=1)
else()
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_OPENMP=0)
endif()

# POSIX threads — set by include("${PARALLEL_SOURCE_DIR}/Cmake/threads.cmake") based on platform detection.
if(PARALLEL_ENABLE_PTHREADS)
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_PTHREADS=1)
else()
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_PTHREADS=0)
endif()

# Win32 threads — set by include("${PARALLEL_SOURCE_DIR}/Cmake/threads.cmake") on Windows.
if(PARALLEL_ENABLE_WIN32_THREADS)
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_WIN32_THREADS=1)
else()
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_WIN32_THREADS=0)
endif()

list(APPEND PARALLEL_DEPENDENCY_COMPILE_DEFINITIONS ${PARALLEL_COMPILE_DEFINITIONS})

# --- Runtime library wiring ---
if(PARALLEL_ENABLE_TBB AND TARGET Tbb::tbb)
  list(APPEND PARALLEL_DEPENDENCY_LIBS Tbb::tbb)
endif()
if(PARALLEL_ENABLE_OPENMP AND TARGET OpenMP::OpenMP_CXX)
  list(APPEND PARALLEL_DEPENDENCY_LIBS OpenMP::OpenMP_CXX)
endif()
if(TARGET Threads::Threads)
  list(APPEND PARALLEL_DEPENDENCY_LIBS Threads::Threads)
endif()

# --- Source discovery ---
set(parallel_headers "")
set(parallel_sources "")
set(CMAKE_CXX_STANDARD ${PARALLEL_CXX_STANDARD})

file(GLOB_RECURSE parallel_headers LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE parallel_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Remove TBB backend files when TBB is not enabled.
if(NOT PARALLEL_ENABLE_TBB)
  file(GLOB_RECURSE TBB_PARALLEL_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/tbb/*.cpp")
  file(GLOB_RECURSE TBB_PARALLEL_HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/tbb/*.h")
  list(REMOVE_ITEM parallel_sources ${TBB_PARALLEL_SOURCE_FILES})
  list(REMOVE_ITEM parallel_headers ${TBB_PARALLEL_HEADER_FILES})
  message(STATUS "TBB disabled: excluding TBB parallel source files from build")
endif()

# Remove OpenMP backend files when OpenMP is not enabled.
if(NOT PARALLEL_ENABLE_OPENMP)
  file(GLOB_RECURSE OPENMP_PARALLEL_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/openmp/*.cpp")
  file(GLOB_RECURSE OPENMP_PARALLEL_HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/openmp/*.h")
  list(REMOVE_ITEM parallel_sources ${OPENMP_PARALLEL_SOURCE_FILES})
  list(REMOVE_ITEM parallel_headers ${OPENMP_PARALLEL_HEADER_FILES})
  message(STATUS "OpenMP disabled: excluding OpenMP parallel source files from build")
endif()

# --- Target definition ---
add_library(Parallel ${parallel_sources} ${parallel_headers})
target_compile_features(Parallel PUBLIC cxx_std_${PARALLEL_CXX_STANDARD})
if(PARALLEL_ENABLE_COVERAGE)
  parallel_configure_coverage(Parallel)
endif()
parallel_target_apply_cache(Parallel PARALLEL_ENABLE_CACHE PARALLEL_CACHE_BACKEND)
parallel_target_apply_lto(Parallel PARALLEL)
if(PARALLEL_ENABLE_SANITIZER)
  parallel_configure_sanitizer(Parallel PARALLEL)
endif()
parallel_find_linker(${PARALLEL_LINKER_CHOICE} Parallel PARALLEL_LTO_MODE)
parallel_target_optional_icecc(Parallel PARALLEL_ENABLE_ICECC)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(Parallel PUBLIC PARALLEL_SHARED_DEFINE PRIVATE PARALLEL_BUILDING_DLL)
  if(COMMAND xsigma_set_target_output_directories)
    xsigma_set_target_output_directories(Parallel)
  endif()
else()
  target_compile_definitions(Parallel PUBLIC PARALLEL_STATIC_DEFINE)
endif()

add_library(Parallel::Parallel ALIAS Parallel)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${parallel_sources} ${parallel_headers})

set_target_properties(
  Parallel PROPERTIES CXX_STANDARD ${PARALLEL_CXX_STANDARD} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS
                                                                                     OFF
)

if(MSVC)
  target_compile_options(Parallel PRIVATE /WX)
else()
  target_compile_options(Parallel PRIVATE -include cstdlib)
endif()

# Include roots: repository root → "include/common/parallel_export.h", "include/tools/parallel_tools.h"
# INSTALL_INTERFACE mirrors the install layout: <prefix>/include/Parallel/include/...
target_include_directories(
  Parallel
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Testing>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Testing>
         $<INSTALL_INTERFACE:include/Parallel/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/Testing/ParallelTest.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Testing/ParallelTest.h" @ONLY
)

if(PARALLEL_DEPENDENCY_INCLUDE_DIRS)
  target_include_directories(Parallel PUBLIC ${PARALLEL_DEPENDENCY_INCLUDE_DIRS})
  message(STATUS "Parallel: extra include dirs: ${PARALLEL_DEPENDENCY_INCLUDE_DIRS}")
endif()

if(PARALLEL_ENABLE_CLANGTIDY)
  include("${PARALLEL_SOURCE_DIR}/Cmake/clang_tidy.cmake")
  parallel_target_clang_tidy(Parallel ${PARALLEL_ENABLE_FIX})
endif()

if(PARALLEL_ENABLE_IWYU)
  include("${PARALLEL_SOURCE_DIR}/Cmake/iwyu.cmake")
  parallel_apply_iwyu(Parallel)
endif()

if(PARALLEL_DEPENDENCY_LIBS)
  target_link_libraries(Parallel PUBLIC ${PARALLEL_DEPENDENCY_LIBS})
  message(STATUS "Parallel: linked dependencies: ${PARALLEL_DEPENDENCY_LIBS}")
endif()

if(PARALLEL_DEPENDENCY_COMPILE_DEFINITIONS)
  target_compile_definitions(Parallel PUBLIC ${PARALLEL_DEPENDENCY_COMPILE_DEFINITIONS})
  message(STATUS "Parallel: compile definitions: ${PARALLEL_DEPENDENCY_COMPILE_DEFINITIONS}")
endif()

# --- Installation ---
install(
  TARGETS Parallel
  EXPORT ParallelTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES
  DESTINATION include/Parallel
)
# Preserve include/ subdirectory structure so installed headers resolve the same
# relative paths they use in the source tree (e.g. "include/common/parallel_export.h").
install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
  DESTINATION include/Parallel/include
  COMPONENT Development
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "*.hxx"
  PATTERN "*.hpp"
)

# --- CMake Package Configuration Files ---
include(CMakePackageConfigHelpers)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/Cmake/ParallelConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/ParallelConfig.cmake" INSTALL_DESTINATION lib/cmake/Parallel
  PATH_VARS CMAKE_INSTALL_PREFIX
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/ParallelConfigVersion.cmake" VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ParallelConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/ParallelConfigVersion.cmake"
        DESTINATION lib/cmake/Parallel COMPONENT Development
)

install(
  EXPORT ParallelTargets
  NAMESPACE Parallel::
  FILE ParallelTargets.cmake
  DESTINATION lib/cmake/Parallel
  COMPONENT Development
)

# --- Testing ---
if(PARALLEL_ENABLE_TESTING)
  add_subdirectory(Testing/Cxx)
endif()

if(PARALLEL_ENABLE_EXAMPLES)
  add_subdirectory(Examples)
endif()

if(PARALLEL_ENABLE_VALGRIND AND PARALLEL_ENABLE_TESTING)
  parallel_apply_valgrind_timeouts()
endif()

include("${PARALLEL_SOURCE_DIR}/Cmake/summary.cmake")
parallel_print_configuration_summary()
