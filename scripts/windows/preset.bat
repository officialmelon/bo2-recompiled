@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%\..\..\default"

cmake --preset win-amd64-relwithdebinfo -DREXSDK_DIR="%REXSDK_DIR%" -DCMAKE_CXX_FLAGS="-mssse3" -DCMAKE_C_FLAGS="-mssse3"

cd /d "%SCRIPT_DIR%\..\..\default_mp"

cmake --preset win-amd64-relwithdebinfo -DREXSDK_DIR="%REXSDK_DIR%" -DCMAKE_CXX_FLAGS="-mssse3" -DCMAKE_C_FLAGS="-mssse3"