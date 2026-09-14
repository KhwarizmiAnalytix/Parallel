# Parallel

[![CI](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/ci.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/ci.yml)
[![Coverage](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/coverage.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Parallel/actions/workflows/coverage.yml)
[![codecov](https://codecov.io/gh/KhwarizmiAnalytix/Parallel/branch/main/graph/badge.svg)](https://codecov.io/gh/KhwarizmiAnalytix/Parallel)

**Parallel execution**: thread pools, task queues, multi-threader API, and **exclusive** SMP backends — **std::thread**, **OpenMP**, or **Intel TBB** (`PARALLEL_BACKEND` in CMake).

Standalone CMake package — any C++ project can consume it via `add_subdirectory`;
[XSigma](https://github.com/KhwarizmiAnalytix/Hisab) is one consumer, not a required host.

---

## Public API

```cpp
#include "Parallel/parallel.h"
```

is the one include client code needs — it pulls in every public class (`parallel_tools`,
`multi_threader`, `threaded_callback_queue`, `threaded_task_queue`), described below. Nothing else
under `Parallel/` should be included directly: `Parallel/common/parallel_tools_impl.h`,
`Parallel/common/parallel_tools_api.h`, `Parallel/tools/parallel.h`'s `backend_type` enum (not to be
confused with the umbrella `Parallel/parallel.h` — different file, same base name), and everything
under `Parallel/openmp/`, `Parallel/std_thread/`, `Parallel/tbb/` are backend-selection machinery,
chosen automatically at compile time from `PARALLEL_BACKEND`. They live in the
`parallel::detail::parallel_impl` namespace; `detail` is the convention marker that nothing in it is
part of the public contract, and it can change shape without notice. (`parallel_tools_api`, the
internal singleton `parallel_tools` forwards to, is literally documented as "Internal API" in its own
test file — see [`Testing/Cxx/TestParallelToolsApi.cpp`](Testing/Cxx/TestParallelToolsApi.cpp) — for
exactly this reason.) The only other header worth including directly is
`Parallel/common/parallel_export.h`, and only if you need the `PARALLEL_API`/`PARALLEL_VISIBILITY`
macros yourself (e.g. to export your own symbols consistently across a shared-library boundary).

### The one entry point: `parallel_tools`

Almost everything you want is one class, reached through the include above.

**Run something in parallel** — `parallel_for(first, last, grain, fn)` splits `[first, last)` into chunks
of about `grain` elements and runs `fn(chunk_first, chunk_last)` for each chunk across the active backend's
threads. `fn` is just a lambda:

```cpp
std::vector<int> data(1'000'000);
parallel_tools::parallel_for(0, data.size(), /*grain=*/1000,
    [&](size_t first, size_t last) {
        for (size_t i = first; i < last; ++i) data[i] *= 2;
    });
```

**Get a value back (reduce)** — `parallel_reduce(first, last, grain, identity, chunk_fn, combine_fn)` is the
same idea, but each chunk computes a partial result and every partial result is folded into one final value.
Two lambdas, no class to write:

```cpp
double sum_of_squares = parallel_tools::parallel_reduce(
    0, data.size(), /*grain=*/1000, /*identity=*/0.0,
    [&](size_t first, size_t last, double init) {          // reduce one chunk
        double partial = init;
        for (size_t i = first; i < last; ++i) partial += data[i] * data[i];
        return partial;
    },
    [](double a, double b) { return a + b; });              // combine two chunks' results
```

`combine_fn` must be associative and commutative (order-independent) — chunks finish in whatever order the
backend's scheduler picks. That's the whole map-reduce API: those two calls cover the large majority of use
cases.

A few more statics round out the class: `initialize(num_threads = 0)` (0 = auto-detect; call once, before
first use, if you want a specific thread count), `estimated_number_of_threads()`, `is_parallel_scope()`
(true when called from inside a worker thread), and `local_scope(config, lambda)` to temporarily override
thread count / backend / nested-parallelism for one call (restored afterward, even if `lambda` throws):

```cpp
parallel_tools::local_scope(parallel_tools::config{4}, [] { /* runs with 4 threads */ });
```

There is currently no way to query the active backend's name from `parallel_tools` itself (only the
internal `parallel_tools_api` exposes `get_backend()`) — backend selection is a build-time choice
(`PARALLEL_BACKEND`), not something client code is expected to branch on at runtime.

<details>
<summary><b>Advanced:</b> a functor class instead of a lambda, for per-thread setup or teardown</summary>

`parallel_for` also accepts a functor object instead of a lambda. Use this only when you need
`Initialize()` (called once per worker thread, before that thread's first chunk) or `Reduce()` (called
once, after every chunk everywhere has finished) — for example, per-thread scratch buffers that would be
wasteful to allocate per chunk:

```cpp
struct scale_by_two
{
    std::vector<int>& data;
    void operator()(size_t first, size_t last)
    {
        for (size_t i = first; i < last; ++i) data[i] *= 2;
    }
};

scale_by_two functor{data};
parallel_tools::parallel_for(0, data.size(), 1000, functor);
```

`parallel_reduce` is built entirely on top of `parallel_for` (a small wrapper functor and a mutex around
the combine step) — for most reductions it is simpler than writing this by hand.

</details>

---

## Advanced / other tools

The rest of the library is special-purpose; most callers never need it. All three are independent of
`parallel_tools` and, like it, already included by `Parallel/parallel.h` — the per-class header column
below is only for reference (e.g. if you'd rather include just one class's header directly).

| Header | Class | Use it for |
|--------|-------|------------|
| `Parallel/tools/multi_threader.h` | `multi_threader` | Running one function across N threads directly, or one function per thread |
| `Parallel/tools/threaded_callback_queue.h` | `threaded_callback_queue` | Fire-and-forget or dependent async tasks, each returning a future-like handle |
| `Parallel/tools/threaded_task_queue.h` | `threaded_task_queue<R, Args...>` | A fixed worker function fed a stream of inputs, producing a stream of outputs |

### `multi_threader` — direct thread control

Lower-level than `parallel_tools`: you get an explicit `thread_info` (with `thread_id`, `number_of_threads`,
and your `user_data`) per thread instead of a `[first, last)` range. Useful when the work isn't a uniform
range, or each thread needs to run *different* code (`set_multiple_method`).

```cpp
#include "Parallel/parallel.h"

void worker(void* data)
{
    auto* info    = static_cast<multi_threader::thread_info*>(data);
    auto* counter = static_cast<std::atomic<int>*>(info->user_data);
    counter->fetch_add(1);
}

multi_threader* mt = multi_threader::create();   // heap-allocated; caller owns it
mt->set_number_of_threads(4);
std::atomic<int> counter{0};
mt->set_single_method(worker, &counter);
mt->single_method_execute();                     // blocks until all 4 threads finish
delete mt;
```

`create()` returns a raw, caller-owned pointer (its constructor is protected — this is the only way to
obtain an instance). `spawn_thread()`/`terminate_thread()` manage individual fire-and-forget threads
outside the single/multiple-method model.

### `threaded_callback_queue` — async tasks with dependencies

`push` enqueues any callable (function pointer, lambda, member-function pointer + object, ...) with its
arguments and returns immediately with a `shared_future`-like handle; the call runs on one of the queue's
worker threads. `get()` blocks until that task's result is ready.

```cpp
#include "Parallel/parallel.h"

threaded_callback_queue queue;
queue.set_number_of_threads(4);                  // default is 1 — always set this

auto future = queue.push([](int x) { return x * x; }, 21);
int  result = queue.get(future);                 // blocks until ready; result == 441
```

`push_dependent(futures, f, args...)` enqueues `f` so it only runs after every future in `futures` has
completed — use it to build a task DAG without manual synchronization. `wait(futures)` blocks until a set
of futures are all done without retrieving their values. All public methods are thread-safe: multiple
threads may `push`/`get` on the same queue concurrently.

### `threaded_task_queue<R, Args...>` — worker-function stream processing

For the common case of "one fixed function, many calls, executed off the calling thread": construct once
with the worker function, then `push` inputs and `pop`/`try_pop` outputs.

```cpp
#include "Parallel/parallel.h"

auto worker = [](int x) { return x * 2; };
threaded_task_queue<int, int> queue(worker, /*strict_ordering=*/true, /*buffer_size=*/-1,
                                     /*max_concurrent_tasks=*/4);
queue.push(5);
int result = 0;
queue.pop(result);                               // blocks until ready; result == 10
```

`strict_ordering` (default `true`) makes `pop`/`try_pop` return results in push order, dropping nothing;
set it `false` to always get the *latest* available result, which lets `buffer_size` discard stale queued
(not-yet-started) inputs once the queue backs up. There is a `threaded_task_queue<void, Args...>`
specialization for fire-and-forget workers (no `pop`, only `push`/`is_empty`/`flush`).

---

## Layout

Library sources live under `Parallel/` (the include root stays the repository root, so consumers
use `#include "Parallel/parallel.h"` etc.); `Testing/` stays at the repository root:

- `CMakeLists.txt` — `PARALLEL_ENABLE_*`; backend from `Cmake/parallel_backend.cmake`.
- `BUILD.bazel` — `//:Parallel`; backend sources via `select`.
- `Parallel/parallel.h` — the public umbrella include (see "Public API" above).
- `Parallel/common/`, `Parallel/tools/` — backend-agnostic core.
- `Parallel/std_thread/`, `Parallel/openmp/`, `Parallel/tbb/` — backend code.
- `Testing/Cxx/` — tests and benchmarks (built only standalone).

---

## CMake options

### Backend (single control point)

| CMake variable | Default | Values |
|----------------|---------|--------|
| `PARALLEL_BACKEND` | `std` | `std`, `openmp`, `tbb` — mutually exclusive; drives `PARALLEL_ENABLE_OPENMP` / `PARALLEL_ENABLE_TBB` |

See [`Cmake/parallel_backend.cmake`](Cmake/parallel_backend.cmake) for how flags are forced when switching backend.

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
Codecov), `lint.yml` (codespell, `clang-format --dry-run`, and a `clang-tidy` build — see below), and
`sanitizers.yml` (ASan/UBSan; advisory — see note below).

Reproduce coverage locally:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPARALLEL_ENABLE_COVERAGE=ON -DPARALLEL_ENABLE_BENCHMARK=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
gcovr --root . --exclude '.*/ThirdParty/.*' --exclude '.*/Testing/.*' --exclude '.*/build/.*' --html --html-details -o build/coverage/index.html
```

### Linting

[`.clang-format`](.clang-format) (Google-based, 4-space indent, Allman braces, 100-column) and
[`.clang-tidy`](.clang-tidy) (bugprone/modernize/readability/misc, curated) apply to everything under
`Parallel/` and `Testing/`. Reproduce both locally:

```sh
# Formatting — auto-fixable
find Parallel Testing -type f \( -name "*.h" -o -name "*.cpp" \) -print0 | xargs -0 clang-format -i

# Static analysis — needs a build (PARALLEL_ENABLE_CLANGTIDY wires it into every target's compile step)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPARALLEL_ENABLE_TESTING=ON -DPARALLEL_ENABLE_BENCHMARK=ON -DPARALLEL_ENABLE_CLANGTIDY=ON
cmake --build build --parallel
```

`PARALLEL_ENABLE_FIX=ON` alongside `PARALLEL_ENABLE_CLANGTIDY` additionally passes `-fix-errors -fix`,
applying clang-tidy's suggested fixes directly to source files — review the diff before committing.

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
