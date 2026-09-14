# Parallel

[![CI](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/ci.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/ci.yml)
[![Coverage](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/coverage.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/coverage.yml)
[![codecov](https://codecov.io/gh/KhwarizmiAnalytix/Parallel/branch/main/graph/badge.svg)](https://codecov.io/gh/KhwarizmiAnalytix/Parallel)

**Parallel execution**: thread pools, task queues, multi-threader API, and **exclusive** SMP backends — **std::thread**, **OpenMP**, or **Intel TBB** (`PARALLEL_BACKEND` in CMake).

Standalone CMake package — any C++ project can consume it via `add_subdirectory`;
[XSigma](https://github.com/KhwarizmiAnalytix/Hisab) is one consumer, not a required host.

## Layout

Library sources live under `Parallel/` (the include root stays the repository root, so consumers
use `#include "Parallel/tools/parallel_tools.h"` etc.); `Testing/` stays at the repository root:

- `CMakeLists.txt` — `PARALLEL_ENABLE_*`; backend from `Cmake/parallel_backend.cmake`.
- `BUILD.bazel` — `//:Parallel`; backend sources via `select`.
- `Parallel/common/`, `Parallel/tools/` — backend-agnostic core.
- `Parallel/std_thread/`, `Parallel/openmp/`, `Parallel/tbb/` — backend code.
- `Testing/Cxx/` — tests and benchmarks (built only standalone).

---

## CMake options

### Backend (single control point)

| CMake variable | Default | Values |
|----------------|---------|--------|
| `PARALLEL_BACKEND` | `std` | `std`, `openmp`, `tbb` — mutually exclusive; drives `PARALLEL_ENABLE_OPENMP` / `PARALLEL_ENABLE_TBB` |

See `Library/Parallel/Cmake/parallel_backend.cmake` for how flags are forced when switching backend.

### Feature and toolchain

| CMake variable | Default | Summary |
|----------------|---------|---------|
| `PARALLEL_LTO_MODE` | Build-type dependent | `off`, `thin`, `full`, `ipo`, or `auto`; non-Debug performance builds default to `auto` |
| `PARALLEL_ENABLE_COVERAGE` | OFF | Coverage |
| `PARALLEL_ENABLE_TESTING` | ON | Tests |
| `PARALLEL_ENABLE_EXAMPLES` | OFF | Examples |
| `PARALLEL_ENABLE_GTEST` | ON | GoogleTest |
| `PARALLEL_ENABLE_BENCHMARK` | ON | Google Benchmark |
| `PARALLEL_ENABLE_ICECC` / `PARALLEL_ENABLE_CACHE` / `PARALLEL_ENABLE_CLANGTIDY` / … | see `CMakeLists.txt` | Tooling |

### `CACHE STRING`

| CMake variable | Default | Notes |
|----------------|---------|-------|
| `PARALLEL_CXX_STANDARD` | 20 | `11`–`23` |
| `PARALLEL_SANITIZER_TYPE` | address | if sanitizer ON |
| `PARALLEL_LINKER_CHOICE` | default | linker |
| `PARALLEL_CACHE_BACKEND` | none | compiler cache |

Thread API macros (`PARALLEL_HAS_PTHREADS` / `PARALLEL_HAS_WIN32_THREADS`) are set from `threads.cmake`.

---

## CI

[`.github/workflows/`](../../.github/workflows/): `ci.yml` — a `cmake` job covering Debug **and** Release on every
platform (gcc + clang on Linux, AppleClang on macOS, MSVC on Windows; 8 jobs), a `cmake-backends` job covering the
OpenMP and TBB backends across all three platforms and both build types (12 jobs, each asserting via
`CMakeCache.txt` that the backend was actually enabled rather than silently falling back to `std`), and a Bazel
build+test job (Linux only). `coverage.yml` (gcov/gcovr line-coverage report, threshold-gated, uploaded to
Codecov), `lint.yml` (codespell), and `sanitizers.yml` (ASan/UBSan; advisory — see note below).

Reproduce coverage locally:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPARALLEL_ENABLE_COVERAGE=ON -DPARALLEL_ENABLE_BENCHMARK=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
gcovr --root . --exclude '.*/ThirdParty/.*' --exclude '.*/Testing/.*' --exclude '.*/build/.*' --html --html-details -o build/coverage/index.html
```

> **Note:** `sanitizers.yml` is advisory (non-blocking, timeout-bounded). AddressSanitizer builds
> cleanly but a full `ParallelCxxTests` run has been observed to hang (a spin-wait likely made
> pathological by ASan's instrumentation slowdown) — UndefinedBehaviorSanitizer is clean. This is
> a known open issue to investigate separately; it is not currently blocking CI.

---

## Bazel flags

Starlark: [`bazel/parallel.bzl`](../../bazel/parallel.bzl). `config_setting` names live in [`bazel/BUILD.bazel`](../../bazel/BUILD.bazel).

### SMP backends (CMake `PARALLEL_BACKEND` parity)

| Define | `config_setting` | Effect |
|--------|------------------|--------|
| `parallel_backend` | `openmp` → `//bazel:parallel_backend_openmp`; `tbb` → `//bazel:parallel_backend_tbb` | Same role as CMake `PARALLEL_BACKEND`: selects OpenMP vs TBB vs the default **std** thread backend (no `config_setting` matches → each `select()`'s `//conditions:default` branch). |
| `parallel_enable_openmp` | `//bazel:enable_openmp` | **Legacy** alias: `PARALLEL_HAS_OPENMP=1` when `parallel_backend` is not already `openmp`. |
| `parallel_enable_tbb` | `//bazel:parallel_enable_tbb` | **Legacy** alias: `PARALLEL_HAS_TBB=1` when `parallel_backend` is not already `tbb`. |

`parallel.bzl` and `BUILD.bazel` use Skylib `selects.with_or` so **`parallel_backend=*` OR the matching legacy `parallel_enable_*`** turns on the same backend. If both OpenMP and TBB flags were true, TBB source selection wins (avoid that — match CMake exclusivity).

`.bazelrc` provides named configs so backend selection reads the same way as CMake's `-DPARALLEL_BACKEND=`: **`bazel build --config=openmp`** sets `parallel_backend=openmp`; **`bazel build --config=tbb`** sets `parallel_backend=tbb`.

### Platform threads

`PARALLEL_HAS_PTHREADS` / `PARALLEL_HAS_WIN32_THREADS` are chosen in `parallel.bzl` from `@platforms//os:windows` vs default (no `define`).

### CMake-only

Benchmarks and tests aren't gated behind a define on the Bazel side — benchmark targets are generated per `Benchmark*.cpp` file and built on request (`bazel build //Testing/Cxx:benchmark_<name>`), and the GoogleTest-based `ParallelCxxTests` target always links gtest. `PARALLEL_CXX_STANDARD` is fixed at `c++20` in `parallel.bzl` rather than configurable. LTO, coverage, sanitizers, linker/cache, spell, Valgrind — **CMake only**.
