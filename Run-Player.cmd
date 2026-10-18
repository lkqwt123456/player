@echo off
setlocal
if exist "%~dp0dist\FluxPlayer\FluxPlayer.exe" (
    start "" /D "%~dp0dist\FluxPlayer" "%~dp0dist\FluxPlayer\FluxPlayer.exe"
    exit /b 0
)
if exist "%~dp0build-native\FluxPlayer.exe" (
    start "" /D "%~dp0build-native" "%~dp0build-native\FluxPlayer.exe"
    exit /b 0
)
if exist "%~dp0build\Release\FluxPlayer.exe" (
    start "" /D "%~dp0build\Release" "%~dp0build\Release\FluxPlayer.exe"
    exit /b 0
)
echo Please build FluxPlayer first. See README.md for instructions.
pause
