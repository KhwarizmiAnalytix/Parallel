# =============================================================================
# Parallel — dependency resolution (standalone + embedded)
# =============================================================================
# Resolves Google Test and Google Benchmark without requiring an XSigma tree.
#
# Resolution order for each dependency:
#   1. Targets already exist (embedded in a host project that provides them)
#      → reuse and return.
#   2. PARALLEL_THIRD_PARTY_DIR is set (host-provided third-party root).
#   3. Vendored nested submodule under this repo's ThirdParty/.
#   4. FetchContent from the upstream git repository.
#
# TBB / OpenMP / Threads are intentionally NOT handled here: they are resolved
# by this repo's own finder modules (Cmake/tbb_multithreading.cmake,
# Cmake/openmp.cmake, Cmake/threads.cmake) in both modes.
# =============================================================================

include_guard(GLOBAL)

include(third_party_helpers)

option(PARALLEL_ENABLE_EXTERNAL "Prefer external/system third-party packages when available" ON)
mark_as_advanced(PARALLEL_ENABLE_EXTERNAL)

# CMAKE_CURRENT_LIST_DIR = this file's directory (<repo>/Cmake) regardless of
# the including directory's scope.
get_filename_component(_parallel_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PARALLEL_THIRD_PARTY_DIR "${_parallel_repo_root}/ThirdParty"
    CACHE PATH "Root of Parallel's bundled third-party sources"
)

# -----------------------------------------------------------------------------
# parallel_setup_gtest: Google Test for the test suite
# -----------------------------------------------------------------------------
function(parallel_setup_gtest)
    if(TARGET gtest_main OR TARGET GTest::gtest_main)
        # Fall through to alias normalization below.
    elseif(COMMAND xsigma_add_googletest)
        # Embedded in XSigma: reuse the host's googletest wiring.
        xsigma_add_googletest()
    elseif(EXISTS "${PARALLEL_THIRD_PARTY_DIR}/googletest/CMakeLists.txt")
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        add_subdirectory("${PARALLEL_THIRD_PARTY_DIR}/googletest"
                         "${CMAKE_BINARY_DIR}/ThirdParty/googletest_build" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.18.0
        )
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    if(TARGET gtest AND NOT TARGET GTest::gtest)
        add_library(GTest::gtest ALIAS gtest)
    endif()
    if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    # XSigma-style namespaced aliases used by this repo's test targets.
    _create_third_party_interface_targets(
        "Gtest::gtest=GTest::gtest|gtest"
        "Gtest::gtest_main=GTest::gtest_main|gtest_main"
    )
endfunction()

# -----------------------------------------------------------------------------
# parallel_setup_benchmark: Google Benchmark for micro-benchmarks
# -----------------------------------------------------------------------------
function(parallel_setup_benchmark)
    if(TARGET benchmark OR TARGET benchmark::benchmark)
        return()
    endif()
    # Prefer the vendored submodule (pinned commit, known-good): some system
    # benchmark packages (e.g. static Homebrew archives) mislink/crash when
    # consumed via an IMPORTED target with a different toolchain.
    set(_bench_src "")
    if(EXISTS "${PARALLEL_THIRD_PARTY_DIR}/benchmark/CMakeLists.txt")
        set(_bench_src "${PARALLEL_THIRD_PARTY_DIR}/benchmark")
    elseif(PARALLEL_ENABLE_EXTERNAL)
        find_package(benchmark QUIET)
        if(benchmark_FOUND)
            message(STATUS "Found external Google Benchmark")
            return()
        endif()
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        string(APPEND CMAKE_CXX_FLAGS " -Wno-c2y-extensions")
    endif()
    if(NOT DEFINED HAVE_STD_REGEX)
        set(HAVE_STD_REGEX 1 CACHE INTERNAL "")
    endif()
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)
    set(BENCHMARK_ENABLE_EXCEPTIONS ON CACHE BOOL "Enable benchmark exceptions" FORCE)
    set(BENCHMARK_ENABLE_LTO OFF CACHE BOOL "Disable benchmark LTO" FORCE)
    set(BENCHMARK_USE_LIBCXX OFF CACHE BOOL "Disable benchmark libcxx" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable benchmark werror" FORCE)
    set(BENCHMARK_FORCE_WERROR OFF CACHE BOOL "Disable benchmark force werror" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)
    set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "Disable benchmark docs install" FORCE)
    set(BENCHMARK_ENABLE_DOXYGEN OFF CACHE BOOL "Disable benchmark doxygen" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable benchmark gtest tests" FORCE)
    set(BENCHMARK_USE_BUNDLED_GTEST OFF CACHE BOOL "Don't use bundled gtest for benchmark" FORCE)
    set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "Don't download dependencies" FORCE)
    if(_bench_src)
        add_subdirectory("${_bench_src}" "${CMAKE_BINARY_DIR}/ThirdParty/benchmark_build"
                         EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(
            benchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.9.4
        )
        FetchContent_MakeAvailable(benchmark)
    endif()
    _set_third_party_folder_properties("benchmark" "${CMAKE_BINARY_DIR}/ThirdParty/benchmark_build")
endfunction()
