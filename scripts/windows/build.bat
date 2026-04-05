@echo off
setlocal

REM Get script root
set ROOT=%~dp0..\..

REM If no args, build default only
if "%~1"=="" (
    echo Building default...
    cd /d "%ROOT%\default"
    cmake --build --preset win-amd64-relwithdebinfo
    goto :end
)

REM Parse args
:parse
if "%~1"=="" goto :end

if /I "%~1"=="default" (
    echo Building default...
    cd /d "%ROOT%\default"
    cmake --build --preset win-amd64-relwithdebinfo
)

if /I "%~1"=="mp" (
    echo Building default_mp...
    cd /d "%ROOT%\default_mp"
    cmake --build --preset win-amd64-relwithdebinfo
)

if /I "%~1"=="both" (
    echo Building both...
    
    cd /d "%ROOT%\default"
    cmake --build --preset win-amd64-relwithdebinfo

    cd /d "%ROOT%\default_mp"
    cmake --build --preset win-amd64-relwithdebinfo
)

shift
goto parse

:end
echo Done.