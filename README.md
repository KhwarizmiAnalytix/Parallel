# Parallel

[![CI](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/ci.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/ci.yml)
[![Coverage](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/coverage.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/coverage.yml)
[![codecov](https://codecov.io/gh/KhwarizmiAnalytix/Parallel/branch/main/graph/badge.svg)](https://codecov.io/gh/KhwarizmiAnalytix/Parallel)

**Parallel execution**: thread pools, task queues, multi-threader API, and **exclusive** SMP backends — **std::thread**, **OpenMP**, or **Intel TBB** (`PARALLEL_BACKEND` in CMake).

Standalone CMake package — any C++ project can consume it via `add_subdirectory`;
[XSigma](https://github.com/KhwarizmiAnalytix/Hisab) is one consumer, not a required host.

## Layout

- `CMakeLists.txt` — `PARALLEL_ENABLE_*`; backend from `Cmake/parallel_backend.cmake`.
- `BUILD.bazel` — `//:Parallel`; backend sources via `select`.
- `std_thread/`, `openmp/`, `tbb/` — backend code.
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

[`.github/workflows/`](../../.github/workflows/): `ci.yml` (CMake matrix — Linux/macOS/Windows × Debug/Release ×
gcc/clang/MSVC, plus dedicated OpenMP/TBB backend jobs — and a Bazel build+test job),
`coverage.yml` (gcov/gcovr line-coverage report, threshold-gated, uploaded to Codecov),
`lint.yml` (codespell), and `sanitizers.yml` (ASan/UBSan; advisory — see note below).

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
| `parallel_backend` | `openmp` → `//bazel:parallel_backend_openmp`; `tbb` → `//bazel:parallel_backend_tbb` | Same role as CMake `PARALLEL_BACKEND`: selects OpenMP vs TBB vs default **std** thread backend (`parallel_backend=std` in root `.bazelrc`). |
| `parallel_enable_openmp` | `//bazel:enable_openmp` | **Legacy** alias: `PARALLEL_HAS_OPENMP=1` when `parallel_backend` is not already `openmp`. |
| `parallel_enable_tbb` | `//bazel:parallel_enable_tbb` | **Legacy** alias: `PARALLEL_HAS_TBB=1` when `parallel_backend` is not already `tbb`. |

`parallel.bzl` and `BUILD.bazel` use Skylib `selects.with_or` so **`parallel_backend=*` OR the matching legacy `parallel_enable_*`** turns on the same backend. If both OpenMP and TBB flags were true, TBB source selection wins (avoid that — match CMake exclusivity).

**`build:openmp`** sets `parallel_backend=openmp`, keeps `parallel_enable_openmp=true`, and adds `-fopenmp` / link flags. **`build:tbb`** sets `parallel_backend=tbb` and `memory_enable_tbb=true` (TBB **allocator** is separate from the Parallel backend, same as CMake).

**Memory** TBB **allocator** only: `memory_enable_tbb` → `//bazel:memory_enable_tbb`.

### Platform threads

`PARALLEL_HAS_PTHREADS` / `PARALLEL_HAS_WIN32_THREADS` are chosen in `parallel.bzl` from `@platforms//os:windows` vs default (no `define`).

### Other

| Mechanism | Effect |
|-----------|--------|
| `parallel_enable_benchmark` | Default ON in `.bazelrc` (CMake parity) |
| `enable_gtest` | Project-wide gtest defines |

### CMake-only

`PARALLEL_CXX_STANDARD` → `c++20` in `parallel.bzl`. LTO, coverage, sanitizers, linker/cache, spell, Valgrind — **CMake only**.
