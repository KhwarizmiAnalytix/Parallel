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
cmake_minimum_required(VERSION 3.16)

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
  include(CTest)
  enable_testing()
  option(BUILD_SHARED_LIBS "Build shared libraries" ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
else()
  set(PARALLEL_STANDALONE OFF)
endif()

# Own CMake modules: toolchain helpers (lto/cache/sanitize/...) and Parallel-owned
# finders (threads/parallel_backend/openmp/tbb_multithreading) all live in Cmake/.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Cmake")

# --- Section 1: Feature flags (PARALLEL_ENABLE_*) --- TBB/OpenMP/thread flags are set by
# module-owned finders in Section 3.

# Link-Time Optimization — disabled when PARALLEL_ENABLE_COVERAGE or PARALLEL_ENABLE_SANITIZER is ON
# (LTO conflicts with instrumentation / sanitizers).
include(lto)
xsigma_lto_compute_default(_lto_default PROFILE PERFORMANCE)
set(PARALLEL_LTO_MODE "${_lto_default}"
    CACHE STRING "LTO mode for Parallel: off | thin | full | ipo | auto"
)
set_property(CACHE PARALLEL_LTO_MODE PROPERTY STRINGS off thin full ipo auto)

# Coverage instrumentation — activates -fprofile-instr-generate / --coverage compiler flags for
# this module only. LTO is automatically skipped when coverage is enabled.
option(PARALLEL_ENABLE_COVERAGE "Enable coverage instrumentation for Parallel tests" OFF)

# Testing — gates add_subdirectory(Testing/Cxx).
option(PARALLEL_ENABLE_TESTING "Build Parallel test suite" ON)

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
set(PARALLEL_CXX_STANDARD "20" CACHE STRING "C++ standard for Parallel. Options: 11 14 17 20 23")
set_property(CACHE PARALLEL_CXX_STANDARD PROPERTY STRINGS 11 14 17 20 23)
if(NOT PARALLEL_CXX_STANDARD MATCHES "^(11|14|17|20|23)$")
  message(FATAL_ERROR "PARALLEL_CXX_STANDARD must be one of: 11 14 17 20 23")
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
include(compiler_checks) # Detects compiler capabilities; sets PROJECT_* flags
include(checks) # Additional feature-check macros
include(build_type) # Validates / normalises CMAKE_BUILD_TYPE
include(platform) # Platform flags (macOS rpath, Windows /utf-8, …)
include(CMakeDependentOption)

include(cache) # Configures per-target compiler cache launcher

# c) Parallel-owned feature finders (Library/Parallel/Cmake/)
include(threads) # Detects pthreads / Win32 threads; sets PARALLEL_ENABLE_PTHREADS
include(parallel_backend) # Validates PARALLEL_BACKEND; derives PARALLEL_ENABLE_TBB/OPENMP
include(openmp) # Finds/configures OpenMP when PARALLEL_ENABLE_OPENMP=ON
include(tbb_multithreading) # Finds/builds Intel TBB when PARALLEL_ENABLE_TBB=ON
if(PARALLEL_ENABLE_SANITIZER)
  include(sanitize)
endif()
if(PARALLEL_ENABLE_SPELL)
  include(spell)
endif()
if(PARALLEL_ENABLE_VALGRIND)
  include(valgrind)
endif()
include(helper_macros)

# d) Coverage gate — appends -fprofile-instr-generate / --coverage; LTO already cleared above.
if(PARALLEL_ENABLE_COVERAGE)
  include(coverage)
elseif(MSVC)
  string(APPEND CMAKE_EXE_LINKER_FLAGS " /DEBUG:FULL")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " /DEBUG:FULL")
endif()

# --- Section 2 (continued): detection-based PARALLEL_HAS_* compile definitions ---

# Intel TBB — set by include(tbb_multithreading).
if(PARALLEL_ENABLE_TBB)
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_TBB=1)
else()
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_TBB=0)
endif()

# OpenMP — set by include(openmp).
if(PARALLEL_ENABLE_OPENMP)
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_OPENMP=1)
else()
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_OPENMP=0)
endif()

# POSIX threads — set by include(threads) based on platform detection.
if(PARALLEL_ENABLE_PTHREADS)
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_PTHREADS=1)
else()
  list(APPEND PARALLEL_COMPILE_DEFINITIONS PARALLEL_HAS_PTHREADS=0)
endif()

# Win32 threads — set by include(threads) on Windows.
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

file(GLOB_RECURSE parallel_headers LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE parallel_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Keep test sources out of the Parallel library target.
list(FILTER parallel_headers EXCLUDE REGEX ".*/Testing/.*")
list(FILTER parallel_sources EXCLUDE REGEX ".*/Testing/.*")

# Keep vendored third-party sources (ThirdParty/ nested submodules) out of the library target.
list(FILTER parallel_headers EXCLUDE REGEX ".*/ThirdParty/.*")
list(FILTER parallel_sources EXCLUDE REGEX ".*/ThirdParty/.*")

# Remove TBB backend files when TBB is not enabled.
if(NOT PARALLEL_ENABLE_TBB)
  file(GLOB_RECURSE TBB_PARALLEL_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tbb/*.cpp")
  file(GLOB_RECURSE TBB_PARALLEL_HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tbb/*.h")
  list(REMOVE_ITEM parallel_sources ${TBB_PARALLEL_SOURCE_FILES})
  list(REMOVE_ITEM parallel_headers ${TBB_PARALLEL_HEADER_FILES})
  message(STATUS "TBB disabled: excluding TBB parallel source files from build")
endif()

# Remove OpenMP backend files when OpenMP is not enabled.
if(NOT PARALLEL_ENABLE_OPENMP)
  file(GLOB_RECURSE OPENMP_PARALLEL_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/openmp/*.cpp")
  file(GLOB_RECURSE OPENMP_PARALLEL_HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/openmp/*.h")
  list(REMOVE_ITEM parallel_sources ${OPENMP_PARALLEL_SOURCE_FILES})
  list(REMOVE_ITEM parallel_headers ${OPENMP_PARALLEL_HEADER_FILES})
  message(STATUS "OpenMP disabled: excluding OpenMP parallel source files from build")
endif()

# --- Target definition ---
add_library(Parallel ${parallel_sources} ${parallel_headers})
xsigma_target_apply_lto(Parallel PARALLEL)
if(PARALLEL_ENABLE_SANITIZER)
  xsigma_configure_sanitizer(PARALLEL)
endif()
xsigma_find_linker(${PARALLEL_LINKER_CHOICE} Parallel PARALLEL_LTO_MODE)
xsigma_target_apply_cache(Parallel PARALLEL_ENABLE_CACHE PARALLEL_CACHE_BACKEND)
xsigma_target_optional_icecc(Parallel PARALLEL_ENABLE_ICECC)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(Parallel PUBLIC PARALLEL_SHARED_DEFINE PRIVATE PARALLEL_BUILDING_DLL)
  if(COMMAND xsigma_set_target_output_directories)
    xsigma_set_target_output_directories(Parallel)
  endif()
else()
  target_compile_definitions(Parallel PUBLIC PARALLEL_STATIC_DEFINE)
endif()

add_library(Parallel::Parallel ALIAS Parallel)

xsigma_module_create_filters("${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(
  Parallel PROPERTIES CXX_STANDARD ${PARALLEL_CXX_STANDARD} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS
                                                                                     OFF
)

if(MSVC)
  target_compile_options(Parallel PRIVATE /WX)
else()
  target_compile_options(Parallel PRIVATE -include cstdlib)
endif()

# Include roots: Library/Parallel → "common/parallel_export.h", "tools/parallel_tools.h"
target_include_directories(
  Parallel
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Testing>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Testing>
         $<INSTALL_INTERFACE:include>
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
  include(clang_tidy)
  xsigma_target_clang_tidy(Parallel ${PARALLEL_ENABLE_FIX})
endif()

if(PARALLEL_ENABLE_IWYU)
  include(iwyu)
  xsigma_apply_iwyu(Parallel)
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
  DESTINATION include
)
install(FILES ${parallel_headers} DESTINATION include/Parallel/Parallel COMPONENT Development)

# --- Testing ---
if(PARALLEL_ENABLE_TESTING)
  add_subdirectory(Testing/Cxx)
endif()

if(PARALLEL_ENABLE_VALGRIND AND PARALLEL_ENABLE_TESTING)
  xsigma_apply_valgrind_timeouts()
endif()

message("")
message("******** Parallel module (CMake flags) ********")
message("  Backend          : ${PARALLEL_BACKEND}")
if(NOT DEFINED PARALLEL_ENABLE_TBB)
  message("  Tbb              : OFF")
else()
  message("  Tbb              : ${PARALLEL_ENABLE_TBB}")
endif()
message("  Openmp           : ${PARALLEL_ENABLE_OPENMP}")
message("  Cxx standard     : ${PARALLEL_CXX_STANDARD}")
message("  Cache            : ${PARALLEL_ENABLE_CACHE}")
message("  Cache backend    : ${PARALLEL_CACHE_BACKEND}")
message("  Icecc            : ${PARALLEL_ENABLE_ICECC}")
xsigma_print_lto_linker_summary(PARALLEL)
message("  Coverage         : ${PARALLEL_ENABLE_COVERAGE}")
message("  Testing          : ${PARALLEL_ENABLE_TESTING}")
message("  Examples         : ${PARALLEL_ENABLE_EXAMPLES}")
message("  Gtest            : ${PARALLEL_ENABLE_GTEST}")
message("  Benchmark        : ${PARALLEL_ENABLE_BENCHMARK}")
message("  Clang-tidy       : ${PARALLEL_ENABLE_CLANGTIDY}")
message("  Fix              : ${PARALLEL_ENABLE_FIX}")
message("  Iwyu             : ${PARALLEL_ENABLE_IWYU}")
message("  Sanitizer        : ${PARALLEL_ENABLE_SANITIZER}")
message("  Sanitizer type   : ${PARALLEL_SANITIZER_TYPE}")
message("  Spell            : ${PARALLEL_ENABLE_SPELL}")
message("  Valgrind         : ${PARALLEL_ENABLE_VALGRIND}")
message("")
message("Build:")
message("  Default C++ standard     : ${CMAKE_CXX_STANDARD}")
message("  C flags                  : ${CMAKE_C_FLAGS}")
message("  CXX flags                : ${CMAKE_CXX_FLAGS}")
message("  Shared linker flags      : ${CMAKE_SHARED_LINKER_FLAGS}")
message("  Static linker flags      : ${CMAKE_STATIC_LINKER_FLAGS}")
message("  Module linker flags      : ${CMAKE_MODULE_LINKER_FLAGS}")
get_directory_property(_summary_compile_defs DIRECTORY ${PROJECT_SOURCE_DIR} COMPILE_DEFINITIONS)
message("  Top-level compile defs   : ${_summary_compile_defs}")
message("")
