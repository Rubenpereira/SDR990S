@echo off
chcp 65001 >nul 2>&1
rem ---------------------------------------------------------------------------
rem  SDR-990S - gera o pacote PORTATIL (.zip) para publicar / dar aos amigos
rem  Nao precisa de instalador: quem baixa descompacta e roda o SDR990S.exe.
rem  Antes: rode COMPILAR.bat. Vai SEM o SDR990S.ini, sem logs, gravacoes,
rem  capturas e textos decodificados (nada pessoal).
rem ---------------------------------------------------------------------------
cd /d "%~dp0"
if not exist "pronto\SDR990S.exe" (
  echo [ERRO] Nao achei pronto\SDR990S.exe - rode COMPILAR.bat antes.
  if /i not "%1"=="nopause" pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$v = (Select-String -Path 'src\ui\Recursos.h' -Pattern 'RXSDR_VERSAO\s+\"([0-9.]+)\"').Matches[0].Groups[1].Value;" ^
  "$tmp = Join-Path $env:TEMP ('SDR990S_' + $v); if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force };" ^
  "New-Item -ItemType Directory $tmp | Out-Null;" ^
  "Get-ChildItem 'pronto' -File | Where-Object { $_.Extension -in '.exe','.dll' } | Copy-Item -Destination $tmp;" ^
  "foreach ($f in 'LEIA-ME.txt','TERCEIROS.txt','LICENSE.txt','LICENSE-DSD.txt','bookmarks.json') { if (Test-Path $f) { Copy-Item $f $tmp } };" ^
  "if (Test-Path 'pronto\decoders') { Copy-Item 'pronto\decoders' $tmp -Recurse;" ^
  "  Get-ChildItem (Join-Path $tmp 'decoders') -Recurse -Include *.on,*.s16,*.f32,*.log,*.wav | Remove-Item -Force };" ^
  "New-Item -ItemType Directory 'Output' -Force | Out-Null;" ^
  "$zip = 'Output\SDR990S_' + $v + '.zip'; if (Test-Path $zip) { Remove-Item $zip };" ^
  "Compress-Archive -Path $tmp -DestinationPath $zip; Remove-Item $tmp -Recurse -Force;" ^
  "Write-Host ''; Write-Host ('[OK] ' + (Resolve-Path $zip)) -ForegroundColor Green;" ^
  "Get-ChildItem $zip | Select-Object Name, @{n='MB';e={[math]::Round($_.Length/1MB,1)}} | Format-Table -AutoSize"
echo   Sem SDR990S.ini nem run.log dentro: cada um comeca com a sua configuracao.
echo.
if /i not "%1"=="nopause" pause
