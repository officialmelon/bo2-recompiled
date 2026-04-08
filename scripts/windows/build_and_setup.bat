@echo off
setlocal enabledelayedexpansion

REM ==========================================================
REM Resolve ROOT directory
REM ==========================================================
for %%i in ("%~dp0..\..") do set "ROOT=%%~fi"

REM Default preset
set "PRESET=win-amd64-relwithdebinfo"

REM Output directory
set "OUT=%ROOT%\out"
if not exist "%OUT%" mkdir "%OUT%"

REM ==========================================================
REM Parse args
REM ==========================================================
set "TARGET_DEFAULT=0"
set "TARGET_MP=0"

:parse
if "%~1"=="" goto after_parse

if /I "%~1"=="sp" set TARGET_DEFAULT=1
if /I "%~1"=="mp" set TARGET_MP=1
if /I "%~1"=="both" (
    set "TARGET_DEFAULT=1"
    set "TARGET_MP=1"
)

echo %~1 | findstr /I "^win-" >nul
if not errorlevel 1 (
    set "PRESET=%~1"
)

shift
goto parse

:after_parse

if %TARGET_DEFAULT%==0 if %TARGET_MP%==0 (
    set "TARGET_DEFAULT=1"
)

echo Using preset: %PRESET%
echo.

REM ==========================================================
REM Execute builds
REM ==========================================================
if %TARGET_DEFAULT%==1 (
    call :build_target default default.exe
    if errorlevel 1 exit /b 1
)

if %TARGET_MP%==1 (
    call :build_target default_mp default_mp.exe
    if errorlevel 1 exit /b 1
)

echo All tasks completed.
exit /b 0


REM ==========================================================
REM Build function (MUST BE AT END)
REM ==========================================================
:build_target
REM %1 = folder
REM %2 = output exe name

echo Building %1...

cd /d "%ROOT%\%1" || (
    echo ERROR: Failed to cd into %1
    exit /b 1
)

cmake --build --preset "%PRESET%"
if errorlevel 1 (
    echo ERROR: Build failed for %1
    exit /b 1
)

set "BUILT_EXE=%ROOT%\%1\out\build\%PRESET%\%2"

if not exist "!BUILT_EXE!" (
    echo ERROR: Built executable not found:
    echo !BUILT_EXE!
    exit /b 1
)

copy /y "!BUILT_EXE!" "%OUT%\%2" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy %1 output
    exit /b 1
)

echo Done building %1
echo.
exit /b 0
