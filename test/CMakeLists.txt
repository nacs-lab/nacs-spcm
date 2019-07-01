#

add_executable(test-output test_output.cpp)
target_link_libraries(test-output nacs-spcm)
set_source_files_properties(test_output.cpp
  PROPERTIES COMPILE_FLAGS "-march=skylake-avx512 -ffp-contract=fast")

add_executable(test-params test_params.cpp)
target_link_libraries(test-params nacs-spcm)
