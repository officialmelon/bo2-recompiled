@echo off
setlocal

:: Build script for Android ARM64 on Windows
:: Requirements:
:: - ANDROID_NDK_HOME set to your NDK path
:: - rexglue SDK installed/available

if "%ANDROID_NDK_HOME%"=="" (
    echo Error: ANDROID_NDK_HOME is not set.
    exit /b 1
)

set "ANDROID_NDK_HOME=%ANDROID_NDK_HOME:\=/%"

if "%CMAKE_EXE%"=="" (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else (
        set "CMAKE_EXE=cmake"
    )
)

if "%REXSDK_DIR%"=="" (
    if exist "%~dp0..\..\..\rexglue-sdk\CMakeLists.txt" (
        for %%I in ("%~dp0..\..\..\rexglue-sdk") do set "REXSDK_DIR=%%~fI"
    )
)
set "REXSDK_DIR=%REXSDK_DIR:\=/%"

set TARGET=%1
if "%TARGET%"=="" set TARGET=both

set PRESET=%2
if "%PRESET%"=="" set PRESET=android-arm64-release

set BUILD_JOBS=%3
if "%BUILD_JOBS%"=="" set BUILD_JOBS=4

echo Building for Android ARM64 (Target: %TARGET%, Preset: %PRESET%)...
echo CMake: %CMAKE_EXE%
if not "%REXSDK_DIR%"=="" echo ReXGlue SDK: %REXSDK_DIR%

if "%TARGET%"=="sp" goto build_sp
if "%TARGET%"=="default" goto build_sp
if "%TARGET%"=="mp" goto build_mp
if "%TARGET%"=="default_mp" goto build_mp
if "%TARGET%"=="both" goto build_both
echo Unknown target: %TARGET%
exit /b 1

:build_sp
call :build_project default
if errorlevel 1 exit /b 1
goto end

:build_mp
call :build_project default_mp
if errorlevel 1 exit /b 1
goto end

:build_both
call :build_project default
if errorlevel 1 exit /b 1
call :build_project default_mp
if errorlevel 1 exit /b 1
goto end

:build_project
set PROJ=%1
echo Processing %PROJ%...
pushd "%PROJ%"
if "%REXSDK_DIR%"=="" (
    "%CMAKE_EXE%" --preset "%PRESET%"
) else (
    "%CMAKE_EXE%" --preset "%PRESET%" -DREXSDK_DIR="%REXSDK_DIR%"
)
if errorlevel 1 (
    popd
    exit /b 1
)
"%CMAKE_EXE%" --build --preset "%PRESET%" -- -j%BUILD_JOBS%
set BUILD_RESULT=%ERRORLEVEL%
popd
exit /b %BUILD_RESULT%

:end
echo Build complete.
echo Native libraries are in:
echo   default\out\build\%PRESET%\libdefault.so
echo   default_mp\out\build\%PRESET%\libdefault_mp.so
