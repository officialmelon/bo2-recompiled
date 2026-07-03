
# default_mp - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(default_mp LANGUAGES CXX)

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
    ../src/native_renderer/backend_d3d12/D3D12LiveRendererBackend.cpp
    ../src/native_renderer/backend_d3d12/D3D12LiveFrameBuilder.cpp
    ../src/native_renderer/replay/NativeRenderReplay.cpp
    ../src/native_renderer/replay/D3D12ReplayBackend.cpp
    ../src/native_renderer/shader_translation/ShaderTranslation.cpp
    ../src/native_renderer/shader_translation/XenosDxbcTranslator.cpp
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
    # Live on-demand Xenos->DXBC translation (SDK translator linked via the
    # ReXGlue graphics library).
    target_compile_definitions(default_mp PRIVATE BO2_HAVE_XENOS_DXBC_TRANSLATOR)
elseif(ANDROID)
    add_library(default_mp SHARED ${DEFAULTMP_SOURCES})
else()
    add_executable(default_mp ${DEFAULTMP_SOURCES})
endif()

rexglue_setup_target(default_mp)

target_include_directories(default_mp PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../src/native_renderer/replay"
)

if(WIN32)
    set(REXGLUE_CHRONO_COMPAT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../src/rexglue_compat/chrono_clock_time_conversion.h")
    target_include_directories(default_mp BEFORE PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/rexglue_compat")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(default_mp PRIVATE
            "/FI${REXGLUE_CHRONO_COMPAT_INCLUDE}"
            /utf-8
            /Zc:preprocessor
            /Zc:char8_t-
        )
    else()
        target_compile_options(default_mp PRIVATE -include "${REXGLUE_CHRONO_COMPAT_INCLUDE}")
    endif()
    target_link_libraries(default_mp PRIVATE d3d12 d3dcompiler dxgi)
endif()
