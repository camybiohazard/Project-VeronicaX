# veronicax - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(veronicax LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(VERONICAX_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(veronicax WIN32 ${VERONICAX_SOURCES})
else()
    add_executable(veronicax ${VERONICAX_SOURCES})
endif()

rexglue_setup_target(veronicax)
