@echo off
rem Liberty Edge - desfaz o preset Ultimate (volta o ini/cfg de antes)
tasklist /FI "IMAGENAME eq GTAIV.exe" 2>nul | find /I "GTAIV.exe" >nul
if not errorlevel 1 (
  echo Feche o GTA IV antes.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0preset.ps1" -Mode restore
pause
