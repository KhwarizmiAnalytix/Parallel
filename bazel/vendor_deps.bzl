"Module extension for vendored deps without MODULE.bazel (googletest, benchmark)."

load("@bazel_tools//tools/build_defs/repo:local.bzl", "local_repository")

def _vendor_deps_impl(mctx):
    # Derive the module root (Parallel/) from a known exported file.
    parallel_bzl = mctx.path(Label("//bazel:parallel.bzl"))
    module_root = parallel_bzl.dirname.dirname

    # googletest and benchmark ship their own BUILD.bazel.
    local_repository(
        name = "com_google_googletest",
        path = str(module_root) + "/ThirdParty/googletest",
    )
    local_repository(
        name = "com_google_benchmark",
        path = str(module_root) + "/ThirdParty/benchmark",
    )

vendor_deps = module_extension(
    implementation = _vendor_deps_impl,
)
