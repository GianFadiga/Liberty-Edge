# Liberty Edge

Melhorias de anti-serrilhado para **GTA IV: The Complete Edition** em 1080p, construídas sobre o
[FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) e compatíveis com o
[modpack da Gillian](https://gillian-guide.github.io/).

> Anti-aliasing improvements for GTA IV built on top of FusionFix 5.1.2. Everything is optional,
> configurable in the FusionFix ini and reversible.

## O que tem

### SMAA+ (pronto)
Substitui o SMAA do FusionFix quando o menu do jogo está em **Anti-aliasing = SMAA**.

| | SMAA do FusionFix | SMAA+ |
|---|---|---|
| Detecção de bordas | brilho (luma) | cor |
| Usa a profundidade da cena | não | sim: mais forte em silhuetas, mais leve em textura |
| Sensibilidade em silhuetas/fios/grades | 0.05 | 0.03 |
| Sensibilidade em detalhe de textura | 0.05 | 0.10 |
| Busca ao longo da borda | 16 / 8 diagonal | 32 / 16 diagonal |

Roda antes do HUD (radar, textos e menus não são afetados) e custa praticamente nada.
Se o shader do SMAA+ não compilar, o FusionFix volta sozinho para o SMAA original e o motivo
fica em `LibertyEdge.log`, na pasta do jogo.

### Supersampling (em desenvolvimento)
Renderizar a cena acima de 1080p e reduzir antes do HUD. Ainda é uma prova de conceito.

## Instalação

Requisitos: GTA IV Complete Edition (1.2.0.59), **FusionFix 5.1.2** instalado.

1. Baixe o `LibertyEdge.zip` da última build (aba **Actions** → build mais recente → *Artifacts*).
2. Extraia a pasta `LibertyEdge` **dentro da pasta do jogo** (ao lado do `GTAIV.exe`).
3. Rode `LibertyEdge\install.bat`. Ele faz backup do FusionFix original, instala o `.asi` e
   acrescenta a seção `[LIBERTYEDGE]` ao `GTAIV.EFLC.FusionFix.ini`.
4. No jogo: **Anti-aliasing = SMAA**.

Para desinstalar: `LibertyEdge\uninstall.bat` (restaura o `.asi` e o `.ini` do backup).

## Configuração (`plugins\GTAIV.EFLC.FusionFix.ini`)

```ini
[LIBERTYEDGE]
SMAAPlus = 1                 ; 0 = SMAA original do FusionFix
TextureThreshold = 0.10      ; maior = texturas mais nítidas
GeometryThreshold = 0.03     ; menor = mais AA em silhuetas, postes, fios
DepthEdgeSensitivity = 0.08  ; salto relativo de profundidade que conta como silhueta
SearchSteps = 32
SearchStepsDiagonal = 16
SMAAPlusDebug = 0            ; 1 = mostra a máscara de bordas
```

`[POSTFX] EnablePreAlphaDepth = 1` (padrão do FusionFix) é necessário para a parte de profundidade.

## Como é construído

O repositório não copia o FusionFix: o GitHub Actions baixa o FusionFix na versão fixada
(`FUSIONFIX_REF` em `.github/workflows/build.yml`), aplica os patches de `patches/`, valida os
shaders com `fxc` e compila em Windows. O resultado é o artefato `LibertyEdge.zip`.

## Licença

GPL-3.0, como o FusionFix, do qual este projeto é derivado. SMAA © 2013 Jorge Jimenez et al. (MIT).
Não redistribui arquivos do jogo nem de outros mods.
