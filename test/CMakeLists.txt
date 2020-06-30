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

add_executable(test-output test_output.cpp)
target_link_libraries(test-output nacs-utils nacs-spcm)
set_source_files_properties(test_output.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

#add_executable(test-output_jz test_output_jz.cpp)
#target_link_libraries(test-output_jz nacs-utils nacs-spcm)
#set_source_files_properties(test_output_jz.cpp
#  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(test-params test_params.cpp)
target_link_libraries(test-params nacs-spcm)
