@echo off
setlocal
set "LE=%~dp0"
set "SH=%~dp0..\update\common\shaders\win32_30"
tasklist /FI "IMAGENAME eq GTAIV.exe" 2>nul | find /I "GTAIV.exe" >nul
if not errorlevel 1 (
  echo Feche o GTA IV antes.
  pause
  exit /b 1
)
copy /Y "%LE%wireaa\gta_wire.fxc" "%SH%\" >nul || goto erro
echo OK: Wire AA LIGADO (fios do Liberty Edge).
pause
exit /b 0
:erro
echo ERRO ao copiar. Use "Executar como administrador".
pause
exit /b 1
