
# bo2_recompiled - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(bo2_recompiled LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(BO2RECOMPILED_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(bo2_recompiled WIN32 ${BO2RECOMPILED_SOURCES})
else()
    add_executable(bo2_recompiled ${BO2RECOMPILED_SOURCES})
endif()

rexglue_setup_target(bo2_recompiled)
