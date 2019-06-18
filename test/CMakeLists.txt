#
add_definitions(-UNDEBUG)

add_executable(test-params test_params.cpp)
target_link_libraries(test-params nacs-spcm)
