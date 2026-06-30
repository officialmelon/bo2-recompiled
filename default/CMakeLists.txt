
# default - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(default LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

foreach(REXGLUE_FFMPEG_TARGET libavutil libavcodec libavformat libswresample libswscale)
    if(TARGET ${REXGLUE_FFMPEG_TARGET})
        target_include_directories(${REXGLUE_FFMPEG_TARGET} BEFORE PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/rexglue_compat")
    endif()
endforeach()

if(TARGET rexcore AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(rexcore PRIVATE -mssse3)
endif()

if(TARGET rexcore AND WIN32)
    set(REXGLUE_CHRONO_COMPAT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../src/rexglue_compat/chrono_clock_time_conversion.h")
    target_include_directories(rexcore BEFORE PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/rexglue_compat")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(rexcore PRIVATE "/FI${REXGLUE_CHRONO_COMPAT_INCLUDE}" /utf-8)
    else()
        target_compile_options(rexcore PRIVATE -include "${REXGLUE_CHRONO_COMPAT_INCLUDE}")
    endif()
endif()

# Sources
set(NATIVE_RENDERER_SOURCES
    ../src/native_renderer/NativeRenderer.cpp
    ../src/native_renderer/NativeRenderCaptureWriter.cpp
    ../src/native_renderer/DebugRenderLog.cpp
    ../src/native_renderer/RenderResourceManager.cpp
    ../src/native_renderer/ShaderManager.cpp
    ../src/native_renderer/TextureManager.cpp
    ../src/native_renderer/BufferManager.cpp
    ../src/native_renderer/HostDetour.cpp
    ../src/native_renderer/backend_null/NullRendererBackend.cpp
    ../src/native_renderer/shader_translation/ShaderTranslation.cpp
)

set(NATIVE_RENDER_REPLAY_SOURCES
    ../src/native_renderer/replay/NativeRenderReplay.cpp
    ../src/native_renderer/replay/D3D12ReplayBackend.cpp
    ../src/native_renderer/replay/NativeRenderReplayTool.cpp
)

set(NATIVE_SHADER_INSPECT_SOURCES
    ../src/native_renderer/replay/NativeShaderInspect.cpp
    ${ReXGlue_SOURCE_DIR}/src/graphics/pipeline/shader/shader.cpp
    ${ReXGlue_SOURCE_DIR}/src/graphics/pipeline/shader/translator.cpp
    ${ReXGlue_SOURCE_DIR}/src/graphics/pipeline/shader/translator_disasm.cpp
    ${ReXGlue_SOURCE_DIR}/src/graphics/format/ucode.cpp
    ${ReXGlue_SOURCE_DIR}/src/graphics/xenos.cpp
)

set(DEFAULT_SOURCES
    src/main.cpp
    src/native_renderer_hooks.cpp
    src/native_input.cpp
    src/stubs.cpp
    src/xam.cpp
    ../common/android_launcher.cpp
    ${NATIVE_RENDERER_SOURCES}
)

if(WIN32)
    add_executable(default WIN32 ${DEFAULT_SOURCES})
elseif(ANDROID)
    add_library(default SHARED ${DEFAULT_SOURCES})
else()
    add_executable(default ${DEFAULT_SOURCES})
endif()

add_executable(native_render_replay ${NATIVE_RENDER_REPLAY_SOURCES})
target_include_directories(native_render_replay PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../src/native_renderer/replay"
)
target_compile_features(native_render_replay PRIVATE cxx_std_23)
if(WIN32)
    target_compile_definitions(native_render_replay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(native_render_replay PRIVATE d3d12 d3dcompiler)
endif()

add_executable(native_shader_inspect ${NATIVE_SHADER_INSPECT_SOURCES})
target_include_directories(native_shader_inspect PRIVATE
    "${ReXGlue_SOURCE_DIR}/include"
    "${ReXGlue_BINARY_DIR}/include"
)
target_compile_features(native_shader_inspect PRIVATE cxx_std_23)
if(WIN32)
    target_compile_definitions(native_shader_inspect PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(native_shader_inspect PRIVATE bcrypt)
endif()
target_link_libraries(native_shader_inspect PRIVATE rexcore rexfilesystem)

rexglue_setup_target(default)

if(WIN32)
    set(REXGLUE_CHRONO_COMPAT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../src/rexglue_compat/chrono_clock_time_conversion.h")
    target_include_directories(default BEFORE PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/rexglue_compat")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(default PRIVATE "/FI${REXGLUE_CHRONO_COMPAT_INCLUDE}" /utf-8)
    else()
        target_compile_options(default PRIVATE -include "${REXGLUE_CHRONO_COMPAT_INCLUDE}")
    endif()
endif()
