#

set(nacs_spcm_HDRS
  spcm.h
  Stream.h)
set(nacs_spcm_SRCS
  spcm.cpp
  data_stream.cpp
  Stream.cpp)
set(nacs_spcm_LINKS ${SLEEF_LIBRARIES} ${SPCM_LIBRARIES} ${DEPS_LIBRARIES})
set_source_files_properties(data_stream.cpp stream.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")
add_definitions("-\"DNACS_EXPORT_LIB_spcm()=\"")

add_library(nacs-spcm SHARED
  ${nacs_spcm_SRCS})
target_link_libraries(nacs-spcm PUBLIC ${nacs_spcm_LINKS})

set_target_properties(nacs-spcm PROPERTIES
  VERSION "${MAJOR_VERSION}.${MINOR_VERSION}"
  SOVERSION "${MAJOR_VERSION}"
  COMPILE_FLAGS "-fvisibility=hidden"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

install(TARGETS nacs-spcm
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
