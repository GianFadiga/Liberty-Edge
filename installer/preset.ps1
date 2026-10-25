# Liberty Edge - preset Ultimate / restaurar. Uso: preset.ps1 -Mode ultimate|restore
param([string]$Mode = 'ultimate')
$ErrorActionPreference = 'Stop'
$le = $PSScriptRoot
$plug = Join-Path (Split-Path $le) 'plugins'
$ini = Join-Path $plug 'GTAIV.EFLC.FusionFix.ini'
$cfg = Join-Path $plug 'GTAIV.EFLC.FusionFix.cfg'
$bk = Join-Path $le 'backup\preset'

function Save-Lines([string]$file, $lines) { [System.IO.File]::WriteAllLines($file, [string[]]$lines, (New-Object System.Text.UTF8Encoding($false))) }

function Set-Key([string]$file, [string]$section, [string]$key, [string]$value) {
    $lines = [System.Collections.Generic.List[string]]([System.IO.File]::ReadAllLines($file))
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match ('^\[' + [regex]::Escape($section) + '\]')) { $start = $i; break } }
    if ($start -lt 0) { $lines.Add(''); $lines.Add("[$section]"); $lines.Add("$key=$value"); Save-Lines $file $lines; return }
    for ($i = $start + 1; $i -lt $lines.Count -and $lines[$i] -notmatch '^\['; $i++) {
        $m = [regex]::Match($lines[$i], '^(' + [regex]::Escape($key) + ')(\s*=\s*)([^\s/]*)(.*)$')
        if ($m.Success) { $lines[$i] = $m.Groups[1].Value + $m.Groups[2].Value + $value + $m.Groups[4].Value; Save-Lines $file $lines; return }
    }
    $lines.Insert($start + 1, "$key=$value")
    Save-Lines $file $lines
}

if ($Mode -eq 'restore') {
    if (-not (Test-Path (Join-Path $bk 'GTAIV.EFLC.FusionFix.ini'))) { Write-Host 'Nenhum backup do preset encontrado.'; exit 1 }
    Copy-Item (Join-Path $bk 'GTAIV.EFLC.FusionFix.ini') $ini -Force
    if (Test-Path (Join-Path $bk 'GTAIV.EFLC.FusionFix.cfg')) { Copy-Item (Join-Path $bk 'GTAIV.EFLC.FusionFix.cfg') $cfg -Force }
    Write-Host 'OK: configuracoes anteriores ao preset restauradas.'
    exit 0
}

if (-not (Test-Path (Join-Path $bk 'GTAIV.EFLC.FusionFix.ini'))) {
    New-Item -ItemType Directory -Force -Path $bk | Out-Null
    Copy-Item $ini $bk -Force
    if (Test-Path $cfg) { Copy-Item $cfg $bk -Force }
    Write-Host 'Backup salvo em LibertyEdge\backup\preset\'
}
# sombras: transicao suave entre as faixas (cascatas) e mapa de sombras em resolucao dobrada
Set-Key $ini 'SHADOWS' 'CascadeBlendSize' '0.4'
Set-Key $ini 'SHADOWS' 'HighResolutionShadows' '1'
# supersampling com filtro proprio + nitidez
Set-Key $ini 'LIBERTYEDGE' 'Downsample' '1'
Set-Key $ini 'LIBERTYEDGE' 'Sharpen' '0.3'
# menu do FusionFix: sombras dos postes, borderless
if (Test-Path $cfg) {
    Set-Key $cfg 'SHADOWS' 'LamppostShadows' '1'
    Set-Key $cfg 'MAIN' 'BorderlessWindowed' '1'
    Set-Key $cfg 'MAIN' 'Windowed' '1'
}
Write-Host 'OK: preset Ultimate aplicado.'
Write-Host 'No jogo: Graficos > Modo de video 3840x2160 (ou 2880x1620 se pesar), Anti-aliasing SMAA+,'
Write-Host 'Distancia de visao e de detalhe em 70 a 100, Sombras Muito alta.'
