/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Benchmark suite for PARALLEL (Symmetric Multi-Processing) module
 * Measures performance of parallel operations across different workload sizes
 */

#include <benchmark/benchmark.h>

#include <algorithm>
#include <numeric>
#include <vector>

#include "tools/parallel_tools.h"

namespace parallel
{

// Benchmark 1: Parallel For with varying sizes
static void BM_ParallelFor_Small(benchmark::State& state)
{
    const int        size = 100;
    std::vector<int> data(size, 0);

    for (auto _ : state)
    {
        parallel_tools::parallel_for(
            0,
            size,
            10,
            [&data](int begin, int end)
            {
                for (int i = begin; i < end; ++i)
                {
                    data[i] = i * 2;
                }
            });
        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_ParallelFor_Small);

static void BM_ParallelFor_Medium(benchmark::State& state)
{
    const int        size = 10000;
    std::vector<int> data(size, 0);

    for (auto _ : state)
    {
        parallel_tools::parallel_for(
            0,
            size,
            100,
            [&data](int begin, int end)
            {
                for (int i = begin; i < end; ++i)
                {
                    data[i] = i * 2;
                }
            });
        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_ParallelFor_Medium);

static void BM_ParallelFor_Large(benchmark::State& state)
{
    const int        size = 1000000;
    std::vector<int> data(size, 0);

    for (auto _ : state)
    {
        parallel_tools::parallel_for(
            0,
            size,
            10000,
            [&data](int begin, int end)
            {
                for (int i = begin; i < end; ++i)
                {
                    data[i] = i * 2;
                }
            });
        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_ParallelFor_Large);

// Benchmark 2: Parallel For with computation
static void BM_ParallelFor_Computation(benchmark::State& state)
{
    const int           size = 100000;
    std::vector<double> data(size, 0.0);

    for (auto _ : state)
    {
        parallel_tools::parallel_for(
            0,
            size,
            1000,
            [&data](int begin, int end)
            {
                for (int i = begin; i < end; ++i)
                {
                    double x = static_cast<double>(i);
                    data[i]  = x * x + 2.0 * x + 1.0;
                }
            });
        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_ParallelFor_Computation);

// Benchmark 6: Grain size comparison
static void BM_ParallelFor_GrainSize(benchmark::State& state)
{
    const int        size       = 100000;
    const int        grain_size = state.range(0);
    std::vector<int> data(size, 0);

    for (auto _ : state)
    {
        parallel_tools::parallel_for(
            0,
            size,
            grain_size,
            [&data](int begin, int end)
            {
                for (int i = begin; i < end; ++i)
                {
                    data[i] = i;
                }
            });
        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_ParallelFor_GrainSize)->Arg(10)->Arg(100)->Arg(1000)->Arg(10000);

// Benchmark 7: Memory-bound vs compute-bound
static void BM_MemoryBound(benchmark::State& state)
{
    const int        size = 1000000;
    std::vector<int> data(size, 0);

    for (auto _ : state)
    {
        parallel_tools::parallel_for(
            0,
            size,
            10000,
            [&data](int begin, int end)
            {
                for (int i = begin; i < end; ++i)
                {
                    data[i] = i;  // Simple memory write
                }
            });
        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * size);
    state.SetLabel("Memory-bound");
}
BENCHMARK(BM_MemoryBound);

static void BM_ComputeBound(benchmark::State& state)
{
    const int           size = 100000;
    std::vector<double> data(size, 0.0);

    for (auto _ : state)
    {
        parallel_tools::parallel_for(
            0,
            size,
            1000,
            [&data](int begin, int end)
            {
                for (int i = begin; i < end; ++i)
                {
                    double x = static_cast<double>(i);
                    // Compute-intensive operation
                    data[i] = x * x * x + 3.0 * x * x + 3.0 * x + 1.0;
                }
            });
        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * size);
    state.SetLabel("Compute-bound");
}
BENCHMARK(BM_ComputeBound);

}  // namespace parallel

BENCHMARK_MAIN();
