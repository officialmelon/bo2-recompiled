@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%\..\.."

REM Default = only default
if "%~1"=="" set ARGS=default
if not "%~1"=="" set ARGS=%*

for %%A in (%ARGS%) do (
    if /I "%%A"=="sp" (
        echo Running codegen for default...
        cd /d "%ROOT%\default"
        rexglue codegen default_config.toml
    )

    if /I "%%A"=="mp" (
        echo Running codegen for default_mp...
        cd /d "%ROOT%\default_mp"
        rexglue codegen default_mp_config.toml
    )

    if /I "%%A"=="both" (
        echo Running codegen for both...

        cd /d "%ROOT%\default"
        rexglue codegen default_config.toml

        cd /d "%ROOT%\default_mp"
        rexglue codegen default_mp_config.toml
    )
)

echo Done.