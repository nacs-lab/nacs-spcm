# Try to find Sleef functionality
# Once done this will define
#
#  SLEEF_FOUND - system has Sleef
#  SLEEF_INCLUDE_DIR - Sleef include directory
#  SLEEF_LIBRARIES - Libraries needed to use Sleef
#

find_path(SLEEF_INCLUDE_DIR sleef.h)
find_library(SLEEF_LIBRARIES NAMES sleef)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sleef DEFAULT_MSG SLEEF_INCLUDE_DIR SLEEF_LIBRARIES)

mark_as_advanced(SLEEF_INCLUDE_DIR SLEEF_LIBRARIES)
