@echo off
setlocal

for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"

set "TARGET_DEFAULT=0"
set "TARGET_MP=0"

call :parse_args %*
if errorlevel 1 exit /b 1

if "%TARGET_DEFAULT%"=="0" if "%TARGET_MP%"=="0" set "TARGET_DEFAULT=1"

if "%TARGET_DEFAULT%"=="1" (
    call :run_codegen default default_config.toml
    if errorlevel 1 exit /b 1
)

if "%TARGET_MP%"=="1" (
    call :run_codegen default_mp default_mp_config.toml
    if errorlevel 1 exit /b 1
)

echo Done.
exit /b 0

:parse_args
:parse_loop
if "%~1"=="" goto :eof

if /I "%~1"=="sp" set "TARGET_DEFAULT=1"
if /I "%~1"=="default" set "TARGET_DEFAULT=1"
if /I "%~1"=="mp" set "TARGET_MP=1"
if /I "%~1"=="default_mp" set "TARGET_MP=1"
if /I "%~1"=="both" (
    set "TARGET_DEFAULT=1"
    set "TARGET_MP=1"
)

if /I "%~1"=="sp" shift & goto parse_loop
if /I "%~1"=="default" shift & goto parse_loop
if /I "%~1"=="mp" shift & goto parse_loop
if /I "%~1"=="default_mp" shift & goto parse_loop
if /I "%~1"=="both" shift & goto parse_loop

echo ERROR: Unknown argument '%~1'.
exit /b 1

:run_codegen
set "PROJECT=%~1"
set "CONFIG_FILE=%~2"

echo Running codegen for %PROJECT%...
cd /d "%ROOT%\%PROJECT%" || (
    echo ERROR: Failed to cd into %PROJECT%
    exit /b 1
)

rexglue codegen "%CONFIG_FILE%"
if errorlevel 1 (
    echo ERROR: Codegen failed for %PROJECT%
    exit /b 1
)

exit /b 0
