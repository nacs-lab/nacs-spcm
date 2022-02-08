cmake_minimum_required(VERSION 3.0)

project(nacs-spcm)

## Setup
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(MAJOR_VERSION 0)
set(MINOR_VERSION 0)
set(PATCH_VERSION 0)

include(GNUInstallDirs)

# Remove rdynamic
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)

find_package(Sleef REQUIRED)
find_package(SPCM REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(LLVM REQUIRED CONFIG NO_SYSTEM_ENVIRONMENT_PATH)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
pkg_check_modules(DEPS REQUIRED nacs-utils>=9.0)
find_package(yaml-cpp REQUIRED)
#pkg_check_modules(PC_ZMQ REQUIRED libzmq>=4.0)
#add_library(zmq SHARED IMPORTED)
#set_target_properties(zmq PROPERTIES
#  INTERFACE_INCLUDE_DIRECTORIES "${PC_ZMQ_INCLUDE_DIRS}"
  # CMake/FindPkgConfig (as of 3.16) does not seem to understand `-isystem`
  # returned from `pkg-config` and puts it in `CFLAGS_OTHER` which was then transformed into
  # -D-isystem causing compilation error.
  # This happens on the zmq package from Ubuntu.
  # Since we don't really need anything from it we'll just ignore this for now...
  # INTERFACE_COMPILE_DEFINITIONS "${PC_ZMQ_CFLAGS_OTHER}"
#  INTERFACE_LINK_OPTIONS "${PC_ZMQ_LDFLAGS_OTHER}"
#  IMPORTED_LOCATION "${PC_ZMQ_LINK_LIBRARIES}")

pkg_check_modules(ZMQ REQUIRED IMPORTED_TARGET libzmq>=4.0)

add_definitions(-Wall -Wextra -g -pthread -fvisibility=hidden -Wconversion
  -fno-math-errno -Wno-sign-conversion -Wno-psabi -D_FILE_OFFSET_BITS=64 ${DEPS_CFLAGS})
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # We have GCC specific warning options that makes Clang unhappy.
  # Adding this unconditionally cause GCC to issue a warning if there are other warnings
  # so only add this conditionally for clang.
  add_definitions(-Wno-unknown-warning-option)
endif()
include_directories(${SLEEF_INCLUDE_DIR} ${SPCM_INCLUDE_DIR} ${DEPS_INCLUDE_DIRS})

set(LINKER_FLAGS "-Wl,--as-needed -Wl,--no-undefined -Wl,--gc-sections -pthread -fno-math-errno")
set(LINKER_FLAGS "${LINKER_FLAGS} -Wl,-Bsymbolic  ${DEPS_LDFLAGS_OTHER}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LINKER_FLAGS}")
set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} ${LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${LINKER_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3 -O0")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${LLVM_INCLUDE_DIRS}")

add_subdirectory(nacs-spcm)
add_subdirectory(test)
