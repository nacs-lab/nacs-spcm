#
add_definitions(-UNDEBUG)

add_executable(test-data_stream_perf test_data_stream_perf.cpp)
target_link_libraries(test-data_stream_perf nacs-utils)
set_source_files_properties(test_data_stream_perf.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(test-data_stream_gen test_data_stream_gen.cpp)
target_link_libraries(test-data_stream_gen nacs-utils)
set_source_files_properties(test_data_stream_gen.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(test-params test_params.cpp)
target_link_libraries(test-params nacs-spcm)
