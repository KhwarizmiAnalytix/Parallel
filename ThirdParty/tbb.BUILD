# =============================================================================
# Intel TBB (Threading Building Blocks) BUILD Configuration
# =============================================================================
# High-performance task scheduling and memory allocation library
# Built from source (oneTBB v2023.1.0)
# =============================================================================

package(default_visibility = ["//visibility:public"])

# =============================================================================
# TBB Core Library
# =============================================================================
# Builds TBB from source instead of linking against system library

cc_library(
    name = "tbb",
    srcs = glob(
        [
            "src/tbb/*.cpp",
        ],
        exclude = [
            "src/tbb/tbbbind.cpp",  # Requires hwloc
        ],
    ),
    hdrs = glob([
        "include/tbb/**/*.h",
        "include/oneapi/**/*.h",
        "src/tbb/*.h",
    ], allow_empty = True),
    includes = [
        "include",
        "src",
    ],
    copts = [
        "-DTBB_USE_DEBUG=0",
        "-D__TBB_BUILD=1",
        "-D__TBB_DYNAMIC_LOAD_ENABLED=0",
        "-D__TBB_SOURCE_DIRECTLY_INCLUDED=1",
        "-Wno-attributes",
        "-Wno-error",
    ] + select({
        "@platforms//os:windows": [
            "/DUSE_WINTHREAD",
            "/D_WIN32_WINNT=0x0A00",
        ],
        "@platforms//os:macos": [
            "-DUSE_PTHREAD",
            "-fPIC",
            "-D_XOPEN_SOURCE=600",  # Required for ucontext.h on macOS
            "-D_DARWIN_C_SOURCE",  # Required for MAP_ANON and other BSD extensions on macOS
            "-faligned-allocation",  # Enable aligned allocation for macOS 10.13+
            "-mmacosx-version-min=10.13",  # Minimum macOS version for aligned allocation
        ],
        "//conditions:default": [
            "-DUSE_PTHREAD",
            "-fPIC",
        ],
    }) + select({
        "@platforms//cpu:x86_64": [
            "-mwaitpkg",  # Enable WAITPKG instructions (_tpause, _umonitor, _umwait) - x86_64 only
            "-mrtm",      # Enable RTM (Restricted Transactional Memory) - x86_64 only
        ],
        "//conditions:default": [],
    }),
    linkopts = select({
        "@platforms//os:windows": [],
        "@platforms//os:macos": ["-lpthread"],
        "//conditions:default": ["-lpthread", "-ldl"],
    }),
    defines = ["__TBB_NO_IMPLICIT_LINKAGE=1"],
)

# =============================================================================
# TBB Malloc Library
# =============================================================================

cc_library(
    name = "tbbmalloc",
    srcs = glob([
        "src/tbbmalloc/*.cpp",
    ], exclude = [
        "src/tbbmalloc/proxy.cpp",
        "src/tbbmalloc/tbbmalloc_proxy.cpp",
    ]),
    hdrs = glob([
        "include/tbb/tbbmalloc.h",
        "include/oneapi/tbb/tbbmalloc.h",
        "src/tbbmalloc/*.h",
        "src/tbbmalloc_proxy/*.h",
    ], allow_empty = True),
    includes = [
        "include",
        "src",
    ],
    copts = [
        "-DTBB_USE_DEBUG=0",
        "-D__TBBMALLOC_BUILD=1",
    ] + select({
        "@platforms//os:windows": [
            "/DUSE_WINTHREAD",
        ],
        "//conditions:default": [
            "-DUSE_PTHREAD",
            "-fPIC",
        ],
    }),
    linkopts = select({
        "@platforms//os:windows": [],
        "@platforms//os:macos": ["-lpthread"],
        "//conditions:default": ["-lpthread", "-ldl"],
    }),
    deps = [":tbb"],  # tbbmalloc needs TBB headers
)

# =============================================================================
# TBB Malloc Proxy Library
# =============================================================================

cc_library(
    name = "tbbmalloc_proxy",
    srcs = glob([
        "src/tbbmalloc/proxy.cpp",
        "src/tbbmalloc/tbbmalloc_proxy.cpp",
    ], allow_empty = True),
    hdrs = glob([
        "include/tbb/tbbmalloc_proxy.h",
        "include/oneapi/tbb/tbbmalloc_proxy.h",
    ], allow_empty = True),
    includes = [
        "include",
        "src",
    ],
    copts = [
        "-DTBB_USE_DEBUG=0",
        "-D__TBBMALLOC_BUILD=1",
    ] + select({
        "@platforms//os:windows": [
            "/DUSE_WINTHREAD",
        ],
        "//conditions:default": [
            "-DUSE_PTHREAD",
            "-fPIC",
        ],
    }),
    deps = [":tbbmalloc"],
)

