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

add_executable(test-output_kw test_output_kw.cpp)
target_link_libraries(test-output_kw nacs-utils nacs-spcm)
set_source_files_properties(test_output_kw.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(HelloWorld test_file.cpp)
target_link_libraries(HelloWorld nacs-utils nacs-spcm)
set_source_files_properties(test_file.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(test-output_kw2 test_output_kw2.cpp)
target_link_libraries(test-output_kw2 nacs-utils nacs-spcm)
set_source_files_properties(test_output_kw2.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(test-output_kw_add_int test_output_kw_add_int.cpp)
target_link_libraries(test-output_kw_add_int nacs-utils nacs-spcm)
set_source_files_properties(test_output_kw_add_int.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(test-output_kw2_add_int test_output_kw2_add_int.cpp)
target_link_libraries(test-output_kw2_add_int nacs-utils nacs-spcm)
set_source_files_properties(test_output_kw2_add_int.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(benchmarks benchmarks.cpp)
target_link_libraries(benchmarks nacs-utils nacs-spcm)
set_source_files_properties(benchmarks.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(test-params test_params.cpp)
target_link_libraries(test-params nacs-spcm)
