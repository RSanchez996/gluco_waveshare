@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  echo Uso: instalar.bat COM5 [--baud 115200] [--instalacion-limpia]
  exit /b 2
)
if not exist ".tools\flash-venv\Scripts\python.exe" (
  py -3 -m venv .tools\flash-venv
  if errorlevel 1 exit /b 1
)
.tools\flash-venv\Scripts\python.exe -m pip install --disable-pip-version-check esptool==4.12.0
if errorlevel 1 exit /b 1
.tools\flash-venv\Scripts\python.exe tools\install.py --port %*
exit /b %errorlevel%
