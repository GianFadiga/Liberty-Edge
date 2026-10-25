@echo off
setlocal
set "LE=%~dp0"
set "PLUG=%~dp0..\plugins"
tasklist /FI "IMAGENAME eq GTAIV.exe" 2>nul | find /I "GTAIV.exe" >nul
if not errorlevel 1 (
  echo Feche o GTA IV antes de desinstalar.
  pause
  exit /b 1
)
if not exist "%LE%backup\GTAIV.EFLC.FusionFix.asi" (
  echo Nao ha backup em LibertyEdge\backup\. Reinstale o FusionFix 5.1.2 para voltar ao original.
  pause
  exit /b 1
)
copy /Y "%LE%backup\GTAIV.EFLC.FusionFix.asi" "%PLUG%\" >nul || goto erro
copy /Y "%LE%backup\GTAIV.EFLC.FusionFix.ini" "%PLUG%\" >nul || goto erro
echo.
echo OK: FusionFix original restaurado.
pause
exit /b 0
:erro
echo ERRO ao copiar. Clique com o botao direito e use "Executar como administrador".
pause
exit /b 1
