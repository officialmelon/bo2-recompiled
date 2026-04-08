@echo off
setlocal EnableDelayedExpansion

for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"

set "PRESET=win-amd64-relwithdebinfo"
set "TARGET_DEFAULT=0"
set "TARGET_MP=0"

:parse_loop
if "%~1"=="" goto after_parse

if /I "%~1"=="sp" set "TARGET_DEFAULT=1"
if /I "%~1"=="default" set "TARGET_DEFAULT=1"
if /I "%~1"=="mp" set "TARGET_MP=1"
if /I "%~1"=="default_mp" set "TARGET_MP=1"
if /I "%~1"=="both" (
    set "TARGET_DEFAULT=1"
    set "TARGET_MP=1"
)

if /I "%~1"=="--preset" (
    if "%~2"=="" (
        echo ERROR: --preset requires a value.
        exit /b 1
    )
    set "PRESET=%~2"
    shift
    shift
    goto parse_loop
)

echo %~1| findstr /R /I "^[a-z][a-z0-9-]*-[a-z0-9][a-z0-9-]*-[a-z0-9][a-z0-9-]*$" >nul
if not errorlevel 1 (
    set "PRESET=%~1"
    shift
    goto parse_loop
)

if /I "%~1"=="sp" shift & goto parse_loop
if /I "%~1"=="default" shift & goto parse_loop
if /I "%~1"=="mp" shift & goto parse_loop
if /I "%~1"=="default_mp" shift & goto parse_loop
if /I "%~1"=="both" shift & goto parse_loop

echo ERROR: Unknown argument '%~1'.
exit /b 1

:after_parse
if "%TARGET_DEFAULT%"=="0" if "%TARGET_MP%"=="0" set "TARGET_DEFAULT=1"

if "%TARGET_DEFAULT%"=="1" (
    echo Configuring default with preset %PRESET%...
    cd /d "%ROOT%\default" || (
        echo ERROR: Failed to cd into default
        exit /b 1
    )
    set "CMAKE_CMD=cmake --preset %PRESET%"
    if defined REXSDK_DIR set "CMAKE_CMD=!CMAKE_CMD! -DREXSDK_DIR=""%REXSDK_DIR%"""
    if /I "%PRESET:~0,4%"=="win-" set "CMAKE_CMD=!CMAKE_CMD! -DCMAKE_CXX_FLAGS=-mssse3 -DCMAKE_C_FLAGS=-mssse3"
    call !CMAKE_CMD!
    if errorlevel 1 (
        echo ERROR: Configure failed for default
        exit /b 1
    )
)

if "%TARGET_MP%"=="1" (
    echo Configuring default_mp with preset %PRESET%...
    cd /d "%ROOT%\default_mp" || (
        echo ERROR: Failed to cd into default_mp
        exit /b 1
    )
    set "CMAKE_CMD=cmake --preset %PRESET%"
    if defined REXSDK_DIR set "CMAKE_CMD=!CMAKE_CMD! -DREXSDK_DIR=""%REXSDK_DIR%"""
    if /I "%PRESET:~0,4%"=="win-" set "CMAKE_CMD=!CMAKE_CMD! -DCMAKE_CXX_FLAGS=-mssse3 -DCMAKE_C_FLAGS=-mssse3"
    call !CMAKE_CMD!
    if errorlevel 1 (
        echo ERROR: Configure failed for default_mp
        exit /b 1
    )
)

echo Preset setup complete.
exit /b 0
