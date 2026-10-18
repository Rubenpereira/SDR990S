@echo off
cd /d "%~dp0pronto"
if not exist SDR990S.exe (
  echo Nao achei pronto\SDR990S.exe - rode COMPILAR.bat primeiro.
  pause
  exit /b 1
)
start "" SDR990S.exe
