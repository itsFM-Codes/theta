@echo off
setlocal EnableExtensions

cd /d "%~dp0"

call build.cmd
if errorlevel 1 exit /b 1

where node >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Node.js was not found in PATH.
    exit /b 1
)

node web\server.js
