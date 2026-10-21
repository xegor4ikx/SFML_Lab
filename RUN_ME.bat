@echo off
cd /d "%~dp0"
echo ============================================================
echo  Lab 5 (variant 23): CMake + vcpkg build, experiment, screenshots
echo  First build: vcpkg downloads and compiles SFML (5-15 minutes).
echo  During screenshots (~2.5 min) do not touch mouse and keyboard.
echo ============================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_and_screenshots.ps1" %*
echo.
echo Log: docs\build_log.txt
pause
