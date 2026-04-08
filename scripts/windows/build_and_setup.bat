@echo off
setlocal EnableDelayedExpansion

for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "OUT=%ROOT%\out"
if not exist "%OUT%" mkdir "%OUT%"

set "PRESET=win-amd64-relwithdebinfo"
set "TARGET_DEFAULT=0"
set "TARGET_MP=0"

call :parse_args %*
if errorlevel 1 exit /b 1

if "%TARGET_DEFAULT%"=="0" if "%TARGET_MP%"=="0" set "TARGET_DEFAULT=1"

echo Using preset: %PRESET%
echo.

call "%~dp0preset.bat" %BUILD_TARGET_ARGS% --preset "%PRESET%"
if errorlevel 1 exit /b 1

if "%TARGET_DEFAULT%"=="1" (
    call :build_target default default.exe
    if errorlevel 1 exit /b 1
)

if "%TARGET_MP%"=="1" (
    call :build_target default_mp default_mp.exe
    if errorlevel 1 exit /b 1
)

echo All tasks completed.
exit /b 0

:parse_args
set "BUILD_TARGET_ARGS="
:parse_loop
if "%~1"=="" goto :eof

if /I "%~1"=="sp" (
    set "TARGET_DEFAULT=1"
    call :append_target_arg sp
    shift
    goto parse_loop
)
if /I "%~1"=="default" (
    set "TARGET_DEFAULT=1"
    call :append_target_arg sp
    shift
    goto parse_loop
)
if /I "%~1"=="mp" (
    set "TARGET_MP=1"
    call :append_target_arg mp
    shift
    goto parse_loop
)
if /I "%~1"=="default_mp" (
    set "TARGET_MP=1"
    call :append_target_arg mp
    shift
    goto parse_loop
)
if /I "%~1"=="both" (
    set "TARGET_DEFAULT=1"
    set "TARGET_MP=1"
    set "BUILD_TARGET_ARGS=both"
    shift
    goto parse_loop
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

echo ERROR: Unknown argument '%~1'.
exit /b 1

:append_target_arg
if "%BUILD_TARGET_ARGS%"=="" (
    set "BUILD_TARGET_ARGS=%~1"
) else (
    if /I not "%BUILD_TARGET_ARGS%"=="both" set "BUILD_TARGET_ARGS=%BUILD_TARGET_ARGS% %~1"
)
exit /b 0

:build_target
set "PROJECT=%~1"
set "OUTPUT_NAME=%~2"
set "BUILT_EXE=%ROOT%\%PROJECT%\out\build\%PRESET%\%OUTPUT_NAME%"

echo Building %PROJECT%...
cd /d "%ROOT%\%PROJECT%" || (
    echo ERROR: Failed to cd into %PROJECT%
    exit /b 1
)

cmake --build --preset "%PRESET%"
if errorlevel 1 (
    echo ERROR: Build failed for %PROJECT%
    exit /b 1
)

if not exist "%BUILT_EXE%" (
    echo ERROR: Built executable not found:
    echo %BUILT_EXE%
    exit /b 1
)

copy /y "%BUILT_EXE%" "%OUT%\%OUTPUT_NAME%" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy %PROJECT% output
    exit /b 1
)

echo Done building %PROJECT%
echo.
exit /b 0
