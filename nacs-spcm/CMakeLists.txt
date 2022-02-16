#

set(nacs_spcm_HDRS
  spcm.h
  Stream.h
  StreamManager.h
  Config.h
  Controller.h
  DummyController.h
  DummyServer.h
  DummyStream.h
  DummyStreamManager.h
  FileManager.h
  FileWriter.h
  SeqCache.h
  IDataCache.h
  Sequence.h
  SerialPort.h
  Server.h
  Trigger.h
  clock.h)
set(nacs_spcm_SRCS
  spcm.cpp
  data_stream.cpp
  Stream.cpp
  StreamManager.cpp
  Config.cpp
  Controller.cpp
  DummyController.cpp
  DummyServer.cpp
  DummyStream.cpp
  DummyStreamManager.cpp
  FileManager.cpp
  FileWriter.cpp
  SeqCache.cpp
  IDataCache.cpp
  Sequence.cpp
  SerialPort.cpp
  Server.cpp
  Trigger.cpp)
set(nacs_spcm_LINKS ${SLEEF_LIBRARIES} ${SPCM_LIBRARIES} ${DEPS_LIBRARIES})
set_source_files_properties(data_stream.cpp DummyStream.cpp DummyStreamManager.cpp Stream.cpp StreamManager.cpp Controller.cpp SeqCache.cpp IDataCache.cpp Sequence.cpp Server.cpp
  PROPERTIES COMPILE_FLAGS "-ffp-contract=fast")
add_definitions("-\"DNACS_EXPORT_LIB_spcm()=\"")
message(" llvm: ${LLVM_DEFINITIONS}")
set_property(SOURCE ${nacs_spcm_SRCS} APPEND_STRING
  PROPERTY COMPILE_FLAGS " ${LLVM_DEFINITIONS} -Wno-conversion")

add_library(nacs-spcm SHARED
  ${nacs_spcm_SRCS})
target_link_libraries(nacs-spcm PUBLIC ${nacs_spcm_LINKS})
target_link_libraries(nacs-spcm PRIVATE LLVM yaml-cpp PkgConfig::ZMQ)
set_target_properties(nacs-spcm PROPERTIES
  VERSION "${MAJOR_VERSION}.${MINOR_VERSION}"
  SOVERSION "${MAJOR_VERSION}"
  COMPILE_FLAGS "-fvisibility=hidden"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

install(TARGETS nacs-spcm
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
