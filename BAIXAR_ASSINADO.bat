@echo off
chcp 65001 >nul
setlocal
title SDR990S - Compilar no GitHub e baixar o exe ASSINADO
cd /d "%~dp0"

set "REPO=Rubenpereira/SDR990S"

echo ==============================================================
echo   SDR990S - exe assinado (GitHub Actions + SignPath Foundation)
echo ==============================================================
echo.
echo   Antes: rode o SUBIR_GITHUB.bat (o GitHub compila o que esta la).
echo   Durante: a SignPath manda um e-mail / mostra no site um pedido
echo   de assinatura - entre em https://app.signpath.io e clique
echo   em APPROVE. Este script espera ate 1 hora.
echo.

where gh >nul 2>nul
if errorlevel 1 goto SEMGH
gh auth status >nul 2>nul
if errorlevel 1 goto SEMLOGIN

echo  Pedindo a compilacao...
gh workflow run compilar.yml --repo %REPO% --ref main
if errorlevel 1 goto FALHOU
rem o GitHub leva uns segundos para listar a execucao nova
ping -n 12 127.0.0.1 >nul
for /f %%i in ('gh run list --repo %REPO% --workflow compilar.yml --limit 1 --json databaseId -q ".[0].databaseId"') do set "RUN=%%i"
if not defined RUN goto FALHOU
echo  Execucao %RUN% - acompanhando (compilar leva uns 5 minutos)...
gh run watch %RUN% --repo %REPO% --exit-status
if errorlevel 1 goto FALHOU

set "DEST=%TEMP%\sdr990s_assinado"
if exist "%DEST%" rmdir /s /q "%DEST%"
gh run download %RUN% --repo %REPO% -n SDR990S-assinado -D "%DEST%"
if errorlevel 1 goto SEMASSINADO
if not exist "%DEST%\SDR990S.exe" goto SEMASSINADO

echo.
echo  Conferindo a assinatura...
powershell -NoProfile -Command "$s = Get-AuthenticodeSignature '%DEST%\SDR990S.exe'; '  Assinatura: ' + $s.Status + '  -  ' + $s.SignerCertificate.Subject; if ($s.Status -ne 'Valid') { exit 1 }"
if errorlevel 1 goto ASSINATURARUIM

copy /y "%DEST%\SDR990S.exe" "pronto\SDR990S.exe" >nul
echo  [OK] pronto\SDR990S.exe agora e o assinado.
echo.
echo  Gerando o pacote...
call GERAR_PACOTE.bat
echo.
echo ==============================================================
echo  PRONTO. Agora e so rodar o PUBLICAR_RELEASE.bat
echo ==============================================================
goto FIM

:SEMGH
echo  [ERRO] O GitHub CLI (gh) nao esta instalado: winget install --id GitHub.cli
goto FIM
:SEMLOGIN
echo  [ERRO] Entre na sua conta do GitHub antes:  gh auth login
goto FIM
:FALHOU
echo  [ERRO] A compilacao no GitHub falhou. Veja em:
echo         https://github.com/%REPO%/actions
goto FIM
:SEMASSINADO
echo  [ERRO] A compilacao terminou mas nao veio exe ASSINADO.
echo         A SignPath ja aprovou o projeto? O secret SIGNPATH_API_TOKEN e a
echo         variavel SIGNPATH_ORGANIZATION_ID estao no repositorio?
echo         (veja ASSINATURA_PASSO_A_PASSO.txt)
goto FIM
:ASSINATURARUIM
echo  [ERRO] O exe baixado nao tem uma assinatura valida. Nada foi trocado.

:FIM
echo.
pause
endlocal
