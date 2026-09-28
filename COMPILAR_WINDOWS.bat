@echo off
setlocal
cd /d "%~dp0\firmware"
py -m pip install --user "platformio>=6.2.0" || exit /b 1
py -m platformio run
