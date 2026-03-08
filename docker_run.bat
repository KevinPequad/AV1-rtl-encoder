@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

echo Building Docker container...
docker build -t av1-encoder "%SCRIPT_DIR%\docker"

echo Running encoder pipeline in Docker...
docker run --rm -it ^
    -v "%SCRIPT_DIR%:/workspace" ^
    av1-encoder ^
    bash /workspace/run.sh

endlocal
