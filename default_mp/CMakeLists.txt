
# default_mp - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(default_mp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(DEFAULTMP_SOURCES
    src/main.cpp
    src/stubs.cpp
)

if(WIN32)
    add_executable(default_mp WIN32 ${DEFAULTMP_SOURCES})
else()
    add_executable(default_mp ${DEFAULTMP_SOURCES})
endif()

rexglue_setup_target(default_mp)
