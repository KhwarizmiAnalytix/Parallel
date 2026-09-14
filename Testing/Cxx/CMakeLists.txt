# =============================================================================
# Parallel Library C++
# Tests
# =============================================================================
include(ParallelDependencies)

parallel_setup_gtest()

# =============================================================================
# Test Source Files
# Discovery
# =============================================================================
file(GLOB_RECURSE test_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/Test*.cpp")

set(TestFiles)
foreach(_test_source IN ITEMS ${test_sources})
  get_filename_component(file_name "${_test_source}" NAME)

  # When TBB is disabled, exclude TBB-specific parallel tests
  if(NOT PARALLEL_ENABLE_TBB)
    if(file_name MATCHES "TestTbb" OR file_name MATCHES "TestTBB")
      message(STATUS "Excluding TBB parallel test (TBB disabled): ${file_name}")
      continue()
    endif()
  endif()

  # When OpenMP is disabled, exclude OpenMP-specific parallel tests
  if(NOT PARALLEL_ENABLE_OPENMP)
    if(file_name MATCHES "TestOpenMP" OR file_name MATCHES "TestOmp")
      message(STATUS "Excluding OpenMP parallel test (OpenMP disabled): ${file_name}")
      continue()
    endif()
  endif()

  list(APPEND TestFiles "${_test_source}")
endforeach()

# =============================================================================
# ParallelCxxTests
# Executable
# =============================================================================
add_executable(ParallelCxxTests ${TestFiles})

# PARALLEL_HAS_GTEST must be set so ParallelTest.h includes gtest (streaming EXPECT_EQ, SUCCEED,
# etc.).
target_compile_definitions(
  ParallelCxxTests PRIVATE PARALLEL_HAS_GTEST=1 PARALLEL_GOOGLE_TEST USE_GTEST _VARIADIC_MAX=10
)

# Workaround for Clang 20/21.x compiler crash in register allocator
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/TestAsyncParallel.cpp" PROPERTIES COMPILE_FLAGS "-O1"
  )
endif()

target_link_libraries(ParallelCxxTests PRIVATE Gtest::gtest Gtest::gtest_main Parallel::Parallel)

if(PARALLEL_ENABLE_CLANGTIDY)
  xsigma_target_clang_tidy(ParallelCxxTests ${PARALLEL_ENABLE_FIX})
endif()

set_target_properties(
  ParallelCxxTests PROPERTIES FOLDER "Tests" VS_DEBUGGER_WORKING_DIRECTORY
                                             "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME ParallelCxxTests COMMAND ParallelCxxTests
         WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

set_tests_properties(ParallelCxxTests PROPERTIES TIMEOUT 120 LABELS "Parallel;Unit")

if(PARALLEL_ENABLE_COVERAGE AND DEFINED PARALLEL_LLVM_PROFILE_FILE)
  set_tests_properties(
    ParallelCxxTests PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=${PARALLEL_LLVM_PROFILE_FILE}"
  )
endif()

if(COMMAND parallel_set_target_output_directories)
  parallel_set_target_output_directories(ParallelCxxTests)
endif()

message(STATUS "✓ ParallelCxxTests registered with CTest")
message(STATUS "  - Working directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# =============================================================================
# Individual Benchmark
# Executables
# =============================================================================
if(PARALLEL_ENABLE_BENCHMARK)
  # --- Google Benchmark setup ---
  parallel_setup_benchmark()
  _create_third_party_interface_targets(
    "Benchmark::benchmark=benchmark::benchmark|benchmark"
    "Benchmark::benchmark_main=benchmark::benchmark_main|benchmark_main"
  )
  if(NOT TARGET Benchmark::benchmark)
    message(FATAL_ERROR "Google Benchmark target unavailable after parallel_setup_benchmark()")
  endif()

  file(GLOB_RECURSE bench_sources LIST_DIRECTORIES false
       "${CMAKE_CURRENT_SOURCE_DIR}/Benchmark*.cpp"
  )

  foreach(_bench_source IN ITEMS ${bench_sources})
    get_filename_component(bench_name "${_bench_source}" NAME_WE)

    string(REPLACE "Benchmark" "" bench_suffix "${bench_name}")
    string(TOLOWER "${bench_suffix}" bench_suffix_lower)
    set(target_name "benchmark_${bench_suffix_lower}")

    add_executable(${target_name} "${_bench_source}")

    target_link_libraries(
      ${target_name} PRIVATE Benchmark::benchmark Benchmark::benchmark_main Parallel::Parallel
    )

    set_target_properties(
      ${target_name} PROPERTIES FOLDER "Benchmarks" OUTPUT_NAME "${target_name}"
    )

    if(COMMAND parallel_set_target_output_directories)
      parallel_set_target_output_directories(${target_name})
    else()
      set_target_properties(
        ${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
      )
    endif()

    add_test(NAME ${target_name} COMMAND ${target_name} --benchmark_min_time=0.01s
             WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    set_tests_properties(
      ${target_name} PROPERTIES TIMEOUT 600 LABELS "Benchmark;Performance;Parallel"
    )

    message(STATUS "✓ Benchmark target configured: ${target_name} (from ${bench_name}.cpp)")
  endforeach()

  list(LENGTH bench_sources bench_count)
  message(STATUS "✓ Individual benchmark executables configured (${bench_count} total)")
endif()
