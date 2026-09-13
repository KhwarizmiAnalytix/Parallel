load("@bazel_skylib//lib:selects.bzl", "selects")
load("@parallel_openmp//:config.bzl", "OPENMP_COPTS", "OPENMP_LINKOPTS")
load("//bazel:xsigma.bzl", "xsigma_copts", "xsigma_defines", "xsigma_linkopts")

# C++ standard for Parallel — mirrors CMake PARALLEL_CXX_STANDARD (default: 20)
PARALLEL_CXX_STD = "c++20"

def parallel_copts():
    # OPENMP_COPTS (from //bazel:openmp_configure.bzl's host probe) only applied when the
    # openmp backend is actually selected -- an empty list otherwise, or when OpenMP wasn't
    # found (in which case //:BUILD.bazel's :openmp_check dep fails the
    # build with an actionable message instead of silently compiling without OpenMP).
    return xsigma_copts(cxx_std = PARALLEL_CXX_STD) + selects.with_or({
        (
            "//bazel:parallel_backend_openmp",
            "//bazel:enable_openmp",
        ): OPENMP_COPTS,
        "//conditions:default": [],
    })

def parallel_defines():
    """Returns compile definitions for Parallel.

    Mirrors CMakeLists.txt: PARALLEL_HAS_* flags.
    Project-wide flags are included via xsigma_defines().
    """
    defines = xsigma_defines()

    # Threading — PARALLEL_HAS_PTHREADS / PARALLEL_HAS_WIN32_THREADS
    # These mirror the values set by Cmake/threads.cmake and are
    # the sole guards used by multi_threader.h / multi_threader.cpp.
    defines += select({
        "@platforms//os:windows": [
            "PARALLEL_HAS_WIN32_THREADS=1",
            "PARALLEL_HAS_PTHREADS=0",
        ],
        "//conditions:default": [
            "PARALLEL_HAS_PTHREADS=1",
            "PARALLEL_HAS_WIN32_THREADS=0",
        ],
    })

    # TBB multithreading — PARALLEL_HAS_TBB (parallel_backend=tbb or legacy parallel_enable_tbb)
    defines += selects.with_or({
        (
            "//bazel:parallel_backend_tbb",
            "//bazel:parallel_enable_tbb",
        ): ["PARALLEL_HAS_TBB=1"],
        "//conditions:default": ["PARALLEL_HAS_TBB=0"],
    })

    # OpenMP — PARALLEL_HAS_OPENMP (parallel_backend=openmp or legacy parallel_enable_openmp)
    defines += selects.with_or({
        (
            "//bazel:parallel_backend_openmp",
            "//bazel:enable_openmp",
        ): ["PARALLEL_HAS_OPENMP=1"],
        "//conditions:default": ["PARALLEL_HAS_OPENMP=0"],
    })

    return defines

def parallel_linkopts():
    return xsigma_linkopts() + selects.with_or({
        (
            "//bazel:parallel_backend_openmp",
            "//bazel:enable_openmp",
        ): OPENMP_LINKOPTS,
        "//conditions:default": [],
    })
