# Injected after project() for the Windows cross build (CMAKE_PROJECT_INCLUDE).
#
# generated/rexglue.cmake runs codegen with rex::rexglue, which the Windows
# SDK provides as rexglue.exe and Linux cannot run. Codegen output is the same
# on every platform, so import the package early and point rex::rexglue at the
# Linux SDK's binary. The later find_package in rexglue.cmake reuses these
# targets.

set(REXGLUE_HOST_CODEGEN "${CMAKE_SOURCE_DIR}/sdk/bin/rexglue" CACHE FILEPATH
    "Host (Linux) rexglue binary used to run codegen in the Windows cross build")
if(NOT EXISTS "${REXGLUE_HOST_CODEGEN}")
  message(FATAL_ERROR "Host codegen binary not found: ${REXGLUE_HOST_CODEGEN}")
endif()

find_package(rexglue 0.10.0 REQUIRED CONFIG)
foreach(_config IN ITEMS "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
  set_property(TARGET rex::rexglue PROPERTY IMPORTED_LOCATION${_config} "${REXGLUE_HOST_CODEGEN}")
endforeach()
message(STATUS "Windows cross build: codegen runs ${REXGLUE_HOST_CODEGEN}")
