@echo off
setlocal
rem Liberty Edge - instalador
rem Coloque a pasta LibertyEdge dentro da pasta do jogo (ao lado do GTAIV.exe) e rode este arquivo.
set "LE=%~dp0"
set "GAME=%~dp0.."
set "PLUG=%~dp0..\plugins"
if not exist "%GAME%\GTAIV.exe" (
  echo Nao encontrei o GTAIV.exe na pasta acima desta.
  echo Coloque a pasta LibertyEdge dentro da pasta do jogo, ao lado do GTAIV.exe.
  pause
  exit /b 1
)
tasklist /FI "IMAGENAME eq GTAIV.exe" 2>nul | find /I "GTAIV.exe" >nul
if not errorlevel 1 (
  echo Feche o GTA IV antes de instalar.
  pause
  exit /b 1
)
if not exist "%PLUG%\GTAIV.EFLC.FusionFix.asi" (
  echo FusionFix nao encontrado em plugins\. Instale o FusionFix 5.1.2 primeiro.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$v=(Get-Item ($env:PLUG+'\GTAIV.EFLC.FusionFix.asi')).VersionInfo.FileVersion; if($v -notlike '5.1.2*'){Write-Host ('FusionFix instalado: '+$v+' - o Liberty Edge exige o FusionFix 5.1.2.'); exit 1}"
if errorlevel 1 (
  pause
  exit /b 1
)
if not exist "%LE%backup\GTAIV.EFLC.FusionFix.asi" (
  mkdir "%LE%backup" 2>nul
  copy /Y "%PLUG%\GTAIV.EFLC.FusionFix.asi" "%LE%backup\" >nul || goto erro
  copy /Y "%PLUG%\GTAIV.EFLC.FusionFix.ini" "%LE%backup\" >nul || goto erro
  echo Backup do FusionFix original salvo em LibertyEdge\backup\
)
copy /Y "%LE%files\plugins\GTAIV.EFLC.FusionFix.asi" "%PLUG%\" >nul || goto erro
findstr /B /C:"[LIBERTYEDGE]" "%PLUG%\GTAIV.EFLC.FusionFix.ini" >nul
if errorlevel 1 (
  >>"%PLUG%\GTAIV.EFLC.FusionFix.ini" echo.
  type "%LE%files\LibertyEdge.ini" >> "%PLUG%\GTAIV.EFLC.FusionFix.ini" || goto erro
)
echo.
echo OK: Liberty Edge instalado.
echo No jogo, deixe Anti-aliasing = SMAA. Ajustes em plugins\GTAIV.EFLC.FusionFix.ini, secao [LIBERTYEDGE].
pause
exit /b 0
:erro
echo ERRO ao copiar. Clique com o botao direito e use "Executar como administrador".
pause
exit /b 1
