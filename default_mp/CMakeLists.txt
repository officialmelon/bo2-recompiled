
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
set(NATIVE_RENDERER_SOURCES
    ../src/native_renderer/NativeRenderer.cpp
    ../src/native_renderer/DebugRenderLog.cpp
    ../src/native_renderer/RenderResourceManager.cpp
    ../src/native_renderer/ShaderManager.cpp
    ../src/native_renderer/TextureManager.cpp
    ../src/native_renderer/BufferManager.cpp
    ../src/native_renderer/HostDetour.cpp
    ../src/native_renderer/backend_null/NullRendererBackend.cpp
    ../src/native_renderer/shader_translation/ShaderTranslation.cpp
)

set(DEFAULTMP_SOURCES
    src/main.cpp
    src/native_renderer_hooks.cpp
    src/native_input.cpp
    src/stubs.cpp
    src/xam.cpp
    ../common/android_launcher.cpp
    ${NATIVE_RENDERER_SOURCES}
)

if(WIN32)
    add_executable(default_mp WIN32 ${DEFAULTMP_SOURCES})
elseif(ANDROID)
    add_library(default_mp SHARED ${DEFAULTMP_SOURCES})
else()
    add_executable(default_mp ${DEFAULTMP_SOURCES})
endif()

rexglue_setup_target(default_mp)
