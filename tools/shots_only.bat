@echo off
rem Only screenshots (the program must already be built by RUN_ME.bat)
cd /d "%~dp0.."
echo Screenshots of SFML_Lab: do not touch mouse and keyboard (~2.5 min).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_and_screenshots.ps1" -ShotsOnly
echo.
echo Log: docs\screenshots_log.txt
pause
