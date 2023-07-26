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

add_executable(test-output_kw2_func test_output_kw2_func.cpp)
target_link_libraries(test-output_kw2_func nacs-utils nacs-spcm)
set_source_files_properties(test_output_kw2_func.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

#add_executable(test_Stream test_Stream.cpp)
#target_link_libraries(test_Stream nacs-utils nacs-spcm)
#set_source_files_properties(test_Stream.cpp
#  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

#add_executable(test_StreamManager test_StreamManager.cpp)
#target_link_libraries(test_StreamManager nacs-utils nacs-spcm)
#set_source_files_properties(test_StreamManager.cpp
#  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

#add_executable(test_Controller test_Controller.cpp)
#target_link_libraries(test_Controller nacs-utils nacs-spcm)
#set_source_files_properties(test_Controller.cpp
#  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(benchmarks benchmarks.cpp)
target_link_libraries(benchmarks nacs-utils nacs-spcm)
set_source_files_properties(benchmarks.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")

add_executable(test-params test_params.cpp)
target_link_libraries(test-params nacs-spcm)

add_executable(test_config test_Config.cpp)
target_link_libraries(test_config nacs-spcm yaml-cpp)

#add_executable(test_dummyServer test_DummyServer.cpp)
#target_link_libraries(test_dummyServer nacs-spcm yaml-cpp zmq)

add_executable(test_trigger test_trigger.cpp)
target_link_libraries(test_trigger nacs-spcm yaml-cpp zmq)
