@echo off
rem Liberty Edge - aplica o preset Ultimate (sombras suaves, sombra em alta resolucao, postes, supersampling + nitidez)
tasklist /FI "IMAGENAME eq GTAIV.exe" 2>nul | find /I "GTAIV.exe" >nul
if not errorlevel 1 (
  echo Feche o GTA IV antes.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0preset.ps1" -Mode ultimate
pause
