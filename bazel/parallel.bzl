load("@bazel_skylib//lib:selects.bzl", "selects")
load("@parallel_openmp//:config.bzl", "OPENMP_COPTS", "OPENMP_LINKOPTS")

# C++ standard for Parallel — mirrors CMake PARALLEL_CXX_STANDARD (default: 20)
PARALLEL_CXX_STD = "c++20"

def parallel_copts():
    # OPENMP_COPTS (from //bazel:openmp_configure.bzl's host probe) only applied when the
    # openmp backend is actually selected -- an empty list otherwise, or when OpenMP wasn't
    # found (in which case //:BUILD.bazel's :openmp_check dep fails the
    # build with an actionable message instead of silently compiling without OpenMP).
    return select({
        "@platforms//os:windows": [
            "/std:" + PARALLEL_CXX_STD,
            "/Zc:__cplusplus",  # expose correct __cplusplus value (mirrors CMake /Zc:__cplusplus)
            "/EHsc",            # structured exception handling
            "/bigobj",          # large object files (mirrors CMake /bigobj)
            "/utf-8",           # UTF-8 source/output encoding (mirrors CMake /utf-8)
            "/wd4244",          # narrowing conversion (mirrors CMake /wd4244)
            "/wd4267",          # size_t → int conversion (mirrors CMake /wd4267)
            "/wd4715",          # not all control paths return (mirrors CMake /wd4715)
            "/wd4018",          # signed/unsigned comparison (mirrors CMake /wd4018)
            "/WX",              # warnings as errors (mirrors CMake /WX)
        ],
        "//conditions:default": [
            "-std=" + PARALLEL_CXX_STD,
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-include",
            "cstdlib",
        ],
    }) + selects.with_or({
        (
            "//bazel:parallel_backend_openmp",
            "//bazel:enable_openmp",
        ): OPENMP_COPTS,
        "//conditions:default": [],
    })

def parallel_defines():
    """Returns compile definitions for Parallel.

    Mirrors CMakeLists.txt: PARALLEL_HAS_* flags.
    Includes the Windows CRT safety defines.
    """
    defines = select({
        "@platforms//os:windows": [
            "_CRT_SECURE_NO_DEPRECATE",
            "_CRT_NONSTDC_NO_DEPRECATE",
            "_CRT_SECURE_NO_WARNINGS",
            "_SCL_SECURE_NO_DEPRECATE",
            "_SCL_SECURE_NO_WARNINGS",
        ],
        "//conditions:default": [],
    })

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
    return select({
        "@platforms//os:windows": [],
        "@platforms//os:macos": [
            "-undefined",
            "dynamic_lookup",
        ],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }) + selects.with_or({
        (
            "//bazel:parallel_backend_openmp",
            "//bazel:enable_openmp",
        ): OPENMP_LINKOPTS,
        "//conditions:default": [],
    })
