
# default - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(default LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(DEFAULT_SOURCES
    src/main.cpp
    src/stubs.cpp
)

if(WIN32)
    add_executable(default WIN32 ${DEFAULT_SOURCES})
else()
    add_executable(default ${DEFAULT_SOURCES})
endif()

rexglue_setup_target(default)
