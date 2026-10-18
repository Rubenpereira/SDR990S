@echo off
chcp 65001 >nul
setlocal
title SDR990S - Enviar para o GitHub
cd /d "%~dp0"

set "REPO=SDR990S"
set "CONTA=Rubenpereira"

echo ==============================================================
echo   SDR990S - envio para github.com/%CONTA%/%REPO%
echo ==============================================================
echo.

where git >nul 2>nul
if errorlevel 1 goto SEMGIT

if not exist ".gitignore" goto SEMIGNORE

if not exist ".git" git init -b main
rem Nome e e-mail dos commits SO deste repositorio. O e-mail e o "noreply"
rem do GitHub: assim o seu e-mail de verdade nao fica publico nos commits.
git config --local user.name "%CONTA%"
git config --local user.email "%CONTA%@users.noreply.github.com"

echo  Selecionando arquivos conforme o .gitignore...
git reset -q >nul 2>nul
git add -A

rem ============================================================
rem  TRAVA DE SEGURANCA - roda ANTES de qualquer envio.
rem  Se algo pessoal ou sigiloso entrou na lista, para tudo aqui.
rem ============================================================
echo  Conferindo se nao entrou nada pessoal...

git diff --cached --name-only > "%TEMP%\sdr990s_lista.txt"

rem arquivos que nunca podem subir
findstr /i /c:".ini" /c:"run.log" /c:"pronto/" /c:"build/" /c:"Output/" /c:"ESTADO_ATUAL" /c:"Capturas/" /c:"Gravacoes/" "%TEMP%\sdr990s_lista.txt" >nul 2>nul
if not errorlevel 1 goto ACHOUPESSOAL
findstr /i /c:"_ssh_agent" /c:"id_ed25519" /c:"id_rsa" /c:".keystore" /c:".pem" "%TEMP%\sdr990s_lista.txt" >nul 2>nul
if not errorlevel 1 goto ACHOUCHAVE

rem Dentro do CONTEUDO dos arquivos: senha das caixas, rede de casa, pasta
rem do usuario e endereco ddns. Os textos sao montados em pedacos de
rem proposito, para nunca aparecerem inteiros neste .bat (que tambem sobe);
rem o ":(exclude)" tira este proprio arquivo da busca.
set "EU=:(exclude)SUBIR_GITHUB.bat"
set "S1=rcp"
set "S2=mxq"
git grep -I --cached -l -e "%S1%%S2%" -- . "%EU%" >nul 2>nul
if not errorlevel 1 goto ACHOUSENHA
set "R1=192.168"
set "R2=.3."
git grep -I --cached -l -F -e "%R1%%R2%" -- . "%EU%" >nul 2>nul
if not errorlevel 1 goto ACHOUREDE
set "U1=Users\\Rub"
set "U2=en"
git grep -I --cached -l -F -e "%U1%%U2%" -- . "%EU%" >nul 2>nul
if not errorlevel 1 goto ACHOUREDE
set "D1=pu1xtb.dd"
set "D2=ns"
git grep -I --cached -l -i -F -e "%D1%%D2%" -- . "%EU%" >nul 2>nul
if not errorlevel 1 goto ACHOUREDE
git grep -I --cached -l -e "BEGIN OPENSSH PRIVATE KEY" -e "BEGIN RSA PRIVATE KEY" -e "BEGIN PRIVATE KEY" -- . "%EU%" >nul 2>nul
if not errorlevel 1 goto ACHOUCHAVE

echo  [OK] Nada pessoal na lista.
echo.

for /f %%N in ('git diff --cached --name-only --diff-filter=ACMR ^| find /c /v ""') do set "QTD=%%N"
if "%QTD%"=="0" goto NADAMUDOU
echo  Serao enviados %QTD% arquivos. Os primeiros:
echo  --------------------------------------------------------------
git diff --cached --name-only --diff-filter=ACMR > "%TEMP%\sdr990s_envio.txt"
set /a LIN=0
for /f "usebackq delims=" %%F in ("%TEMP%\sdr990s_envio.txt") do call :MOSTRA "%%F"
echo  --------------------------------------------------------------
echo  Lista completa: %TEMP%\sdr990s_envio.txt
echo.
set "OK="
set /p "OK=Confirma o envio? [S/N]: "
if /I not "%OK%"=="S" goto CANCELADO

git commit -m "Atualizacao %date% %time%"
git remote remove origin >nul 2>nul
git remote add origin https://github.com/%CONTA%/%REPO%.git

echo.
echo  Enviando...
git push -u origin main
if errorlevel 1 goto FALHAPUSH

echo.
echo ==============================================================
echo  PRONTO! Publicado em:
echo  https://github.com/%CONTA%/%REPO%
echo  Para o .zip aparecer em Releases: GERAR_PACOTE.bat e depois
echo  PUBLICAR_RELEASE.bat
echo ==============================================================
goto FIM

:MOSTRA
set /a LIN+=1
if %LIN% leq 15 echo    %~1
if %LIN%==16 echo    ... e mais arquivos ^(veja a lista completa no arquivo acima^)
goto :eof

:SEMGIT
echo  ERRO: Git nao encontrado no PATH.
echo  Instale o "Git for Windows": https://git-scm.com/download/win
goto FIM

:SEMIGNORE
echo  ERRO: o arquivo .gitignore nao existe nesta pasta.
echo  Sem ele o envio subiria a pasta inteira, incluindo a configuracao
echo  com o endereco das suas caixas e centenas de MB de compilacao.
echo  Envio cancelado por seguranca.
goto FIM

:ACHOUPESSOAL
echo.
echo ==============================================================
echo  PAROU POR SEGURANCA
echo  Um arquivo pessoal ^(configuracao, log, gravacao, captura ou
echo  pasta pronto/build/Output^) entrou na lista de envio.
echo  Confira o .gitignore. Lista: %TEMP%\sdr990s_lista.txt
echo ==============================================================
git reset >nul 2>nul
goto FIM

:ACHOUCHAVE
echo.
echo ==============================================================
echo  PAROU POR SEGURANCA
echo  Uma chave ou credencial entrou na lista de envio.
echo  Confira o .gitignore. Lista: %TEMP%\sdr990s_lista.txt
echo ==============================================================
git reset >nul 2>nul
goto FIM

:ACHOUSENHA
echo.
echo ==============================================================
echo  PAROU POR SEGURANCA
echo  Algum arquivo da lista contem a senha das caixas.
echo ==============================================================
git reset >nul 2>nul
goto FIM

:ACHOUREDE
echo.
echo ==============================================================
echo  PAROU POR SEGURANCA
echo  Algum arquivo da lista contem dado da sua rede ou do seu PC
echo  ^(IP de casa, pasta do usuario ou endereco ddns^).
echo  Para ver qual: git grep --cached -n "192.168"
echo ==============================================================
git reset >nul 2>nul
goto FIM

:NADAMUDOU
echo  Nada mudou desde o ultimo envio. Nada foi enviado.
git reset >nul 2>nul
goto FIM

:CANCELADO
echo  Envio cancelado por voce. Nada foi enviado.
git reset >nul 2>nul
goto FIM

:FALHAPUSH
echo.
echo  ERRO no envio. Causas comuns:
echo   - o repositorio ainda nao foi criado em https://github.com/new
echo     ^(nome: SDR990S, sem README nem licenca^)
echo   - o login no navegador foi cancelado
echo   - o repositorio ja tem conteudo diferente; nesse caso rode:
echo       git pull --rebase origin main
echo     e depois este .bat de novo

:FIM
echo.
pause
endlocal
