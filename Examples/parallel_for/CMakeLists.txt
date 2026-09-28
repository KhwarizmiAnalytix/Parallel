add_executable(parallel_for_example main.cpp)

target_link_libraries(parallel_for_example PRIVATE Parallel::Parallel)

set_target_properties(parallel_for_example PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME Example.parallel_for COMMAND parallel_for_example)
