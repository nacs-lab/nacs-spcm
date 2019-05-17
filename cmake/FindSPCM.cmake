# Try to find SPCM driver
# Once done this will define
#
#  SPCM_FOUND - system has SPCM
#  SPCM_INCLUDE_DIR - SPCM include directory
#  SPCM_LIBRARIES - Libraries needed to use SPCM
#

find_path(SPCM_INCLUDE_DIR spcm/spcm.h)
find_library(SPCM_LIBRARIES NAMES spcm_linux)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SPCM DEFAULT_MSG SPCM_INCLUDE_DIR SPCM_LIBRARIES)

mark_as_advanced(SPCM_INCLUDE_DIR SPCM_LIBRARIES)
