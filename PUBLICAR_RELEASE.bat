@echo off
chcp 65001 >nul
setlocal
title SDR990S - Publicar release no GitHub
cd /d "%~dp0"

echo ==============================================================
echo   SDR990S - publicar release no GitHub
echo   https://github.com/Rubenpereira/SDR990S/releases
echo ==============================================================
echo.
echo   A versao vem do NOME do pacote Output\SDR990S_x.x.x.zip e as
echo   novidades vem do CHANGELOG.md ^(secao "## SDR990S x.x.x"^).
echo.
echo   Antes de rodar: GERAR_PACOTE.bat e o CHANGELOG atualizado.
echo.

if not exist "publicar_release.ps1" goto SEMSCRIPT

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0publicar_release.ps1"
goto FIM

:SEMSCRIPT
echo  [ERRO] Falta o arquivo publicar_release.ps1 nesta pasta.

:FIM
echo.
pause
endlocal
