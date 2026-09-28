cmake_minimum_required(VERSION 3.16)

# When built standalone (e.g. against an installed prefix), bootstrap a project.
# When built as part of the main Parallel tree, PARALLEL_STANDALONE is already set.
if(NOT DEFINED PARALLEL_STANDALONE)
  project(ParallelExamples LANGUAGES CXX)
  find_package(Threads REQUIRED)
  find_package(Parallel REQUIRED CONFIG)
endif()

add_subdirectory(parallel_for)
