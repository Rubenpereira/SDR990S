# ============================================================================
#  publicar_release.ps1 - cria (ou corrige) a release do SDR990S no GitHub
#
#  Nada aqui e digitado a mao: o numero da versao vem do NOME do pacote
#  Output\SDR990S_x.x.x.zip e as novidades vem da secao "## SDR990S x.x.x"
#  do CHANGELOG.md. A imagem do radio (SDR990S.png, que esta no repositorio)
#  aparece no topo do texto da release.
#
#  Chamado pelo PUBLICAR_RELEASE.bat.
# ============================================================================

$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $raiz
$repo = 'Rubenpereira/SDR990S'

function Erro($msg) { Write-Host ""; Write-Host "  [ERRO] $msg" -ForegroundColor Red; Write-Host ""; exit 1 }
function Info($msg) { Write-Host "  $msg" }

# ---- 1) o gh esta instalado e autenticado? ---------------------------------
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    $env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" +
                [Environment]::GetEnvironmentVariable("Path","User")
}
$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $gh) {
    $candidatos = @(
        "$env:ProgramFiles\GitHub CLI\gh.exe",
        "${env:ProgramFiles(x86)}\GitHub CLI\gh.exe",
        "$env:LOCALAPPDATA\Programs\GitHub CLI\gh.exe"
    )
    $gh = $candidatos | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $gh) {
    Write-Host ""
    Write-Host "  O GitHub CLI (gh) nao foi encontrado." -ForegroundColor Yellow
    Write-Host "  Instale com:  winget install --id GitHub.cli"
    Write-Host "  Depois FECHE e ABRA o PowerShell e rode:  gh auth login"
    Write-Host ""
    exit 1
}
Set-Alias gh $gh -Scope Script
gh auth status *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "  Voce ainda nao entrou na sua conta do GitHub." -ForegroundColor Yellow
    Write-Host "  Rode:  gh auth login"
    Write-Host ""
    exit 1
}

# ---- 2) acha o pacote e tira a versao do nome --------------------------------
$zip = Get-ChildItem "Output\SDR990S_*.zip" -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $zip) { Erro "Nao achei o pacote em Output\. Rode GERAR_PACOTE.bat antes." }
if ($zip.Name -notmatch 'SDR990S_(\d+\.\d+\.\d+)\.zip') { Erro "Nao consegui ler a versao do nome '$($zip.Name)'." }
$ver = $Matches[1]
$tag = "v$ver"
$titulo = "SDR990S $ver"
Info "Versao detectada: $ver  (tag $tag)"
Info "  anexo: $($zip.Name)"

# Trava: o pacote nao pode levar configuracao, log, gravacao nem captura
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::OpenRead($zip.FullName)
$proibidos = $z.Entries | Where-Object { $_.FullName -match '\.(ini|log|wav|pedido)$|Capturas|Gravacoes|Decodificados' }
$z.Dispose()
if ($proibidos) { Erro "O pacote tem arquivo pessoal ($(($proibidos | Select-Object -First 3 | ForEach-Object FullName) -join ', ')). Rode GERAR_PACOTE.bat de novo." }

# ---- 3) monta o texto: imagem + download + novidades do CHANGELOG -------------
if (-not (Test-Path "CHANGELOG.md")) { Erro "CHANGELOG.md nao encontrado." }
$chg = Get-Content "CHANGELOG.md" -Raw -Encoding UTF8
$vEsc = [regex]::Escape($ver)
$m = [regex]::Match($chg, "(?ms)^##\s+SDR990S\s+$vEsc\s*\r?\n(.*?)(?=^##\s|\z)")
if ($m.Success) { $novidades = $m.Groups[1].Value.Trim() }
else {
    Info "Aviso: o CHANGELOG nao tem '## SDR990S $ver'. A release ira sem a lista de novidades."
    $novidades = "_Veja o CHANGELOG.md._"
}

$linhas = @()
$linhas += "![SDR990S](https://raw.githubusercontent.com/$repo/main/SDR990S.png)"
$linhas += ""
$linhas += "## Download"
$linhas += ""
$linhas += "| Arquivo | Windows |"
$linhas += "| --- | --- |"
$linhas += "| **$($zip.Name)** | 7 SP1, 8, 10 e 11 |"
$linhas += ""
$linhas += "Nao precisa instalar: descompacte a pasta e rode o **SDR990S.exe**. Aparelhos: RTL-SDR, RTL-TCP e SDRplay (com a [API oficial](https://www.sdrplay.com/api/))."
$linhas += ""
$linhas += "Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/)."
$linhas += ""
$linhas += "## Novidades desta vers$([char]0xE3)o"
$linhas += ""
$linhas += $novidades
$linhas += ""
$linhas += "Hist$([char]0xF3)rico completo em [CHANGELOG.md](https://github.com/$repo/blob/main/CHANGELOG.md)."

$corpo = Join-Path $env:TEMP "sdr990s_release_$ver.md"
[IO.File]::WriteAllText($corpo, ($linhas -join "`r`n"), (New-Object Text.UTF8Encoding $false))

Write-Host ""
Write-Host "  ---------------- previa do texto ----------------" -ForegroundColor DarkGray
Get-Content $corpo -Encoding UTF8 | Select-Object -First 16 | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
Write-Host "  ..." -ForegroundColor DarkGray
Write-Host "  ------------------------------------------------" -ForegroundColor DarkGray
Write-Host ""

$ok = Read-Host "  Publicar a release $tag ? [s/N]"
if ($ok -ne 's' -and $ok -ne 'S') { Write-Host "  Cancelado. Nada foi enviado."; exit 0 }

# ---- 4) cria ou atualiza -----------------------------------------------------
$prefAnterior = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
gh release view $tag --repo $repo 2>&1 | Out-Null
$existe = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $prefAnterior

if ($existe) {
    Info "A release $tag ja existe - atualizando o texto e o anexo."
    gh release edit $tag --repo $repo --title $titulo --notes-file $corpo
    if ($LASTEXITCODE -ne 0) { Erro "falha ao atualizar o texto." }
    gh release upload $tag $zip.FullName --repo $repo --clobber
    if ($LASTEXITCODE -ne 0) { Erro "falha ao enviar o anexo." }
    $antigos = gh release view $tag --repo $repo --json assets -q '.assets[].name' | Where-Object { $_ -ne $zip.Name }
    foreach ($a in $antigos) { Info "Removendo anexo antigo: $a"; gh release delete-asset $tag $a --repo $repo -y }
} else {
    Info "Criando a release $tag."
    gh release create $tag $zip.FullName --repo $repo --title $titulo --notes-file $corpo --latest
    if ($LASTEXITCODE -ne 0) { Erro "falha ao criar a release. O repositorio ja tem o codigo? Rode SUBIR_GITHUB.bat antes." }
}

Write-Host ""
Write-Host "  PRONTO: https://github.com/$repo/releases/tag/$tag" -ForegroundColor Green
Write-Host ""
