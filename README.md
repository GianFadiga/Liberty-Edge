# Liberty Edge

Melhorias de anti-serrilhado para **GTA IV: The Complete Edition** em 1080p, construídas sobre o
[FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) e compatíveis com o
[modpack da Gillian](https://gillian-guide.github.io/).

> Anti-aliasing improvements for GTA IV built on top of FusionFix 5.1.2. Everything is optional,
> configurable in the FusionFix ini and reversible.

## O que tem

### SMAA+ (pronto)
Novas opções no menu do jogo, em **Gráficos → Anti-aliasing**: `Off / FXAA / SMAA / SMAA+ / SMAA+ Debug`.
Dá para alternar com o jogo rodando e comparar na hora. `SMAA+ Debug` mostra a máscara de bordas
(vermelho/verde = bordas tratadas, azul = bordas de profundidade).

| | SMAA do FusionFix | SMAA+ |
|---|---|---|
| Detecção de bordas | brilho (luma) | cor |
| Usa a profundidade da cena | não | sim: mais forte em silhuetas, mais leve em textura |
| Sensibilidade em silhuetas/fios/grades | 0.05 | 0.03 |
| Sensibilidade em detalhe de textura | 0.05 | 0.10 |
| Busca ao longo da borda | 16 / 8 diagonal | 32 / 16 diagonal |

Roda antes do HUD (radar, textos e menus não são afetados) e custa praticamente nada.
Se o shader do SMAA+ não compilar, o jogo usa o SMAA original, o rodapé avisa e o motivo
fica em `LibertyEdge.log`, na pasta do jogo.

### Supersampling / Downsample (experimental)
Renderiza o jogo acima da resolução do monitor (ex.: 3840×2160) e reduz para a tela, como o DSR/VSR.
Com `Downsample = 1`, janela sem borda e `-width 3840 -height 2160` nas opções de inicialização,
a janela fica do tamanho do monitor e o DXVK reduz a imagem na apresentação.

### Wire AA (em teste)
Os fios de eletricidade do PC são desenhados no modo "deferred", que não tem o engrossamento por
distância que o próprio jogo usa no modo antigo; por isso eles viram tracejados ao longe. O Liberty Edge
substitui o `gta_wire.fxc` do FusionFix por uma versão em que o fio nunca fica mais fino que 1 pixel
de saída e a parte "que sobra" é compensada por pontilhado (o filtro de stipple do FusionFix e o SMAA
suavizam o pontilhado). O shader é gerado pelo CI a partir do código do FusionShaders com o
RageShaderEditor, e o CI confere se a ferramenta reproduz o arquivo original antes de aplicar a mudança.

### Rodapé de informações
Na aba **Gráficos** do menu de pausa aparece uma linha como a do FusionFix:
`Liberty Edge v0.2: AA SMAA+; Internal 3840x2160 -> Output 1920x1080 (supersampling 4.00x)`.

## Instalação

Requisitos: GTA IV Complete Edition (1.2.0.59), **FusionFix 5.1.2** instalado.

1. Baixe o `LibertyEdge.zip` da última build (aba **Actions** → build mais recente → *Artifacts*).
2. Extraia a pasta `LibertyEdge` **dentro da pasta do jogo** (ao lado do `GTAIV.exe`).
3. Rode `LibertyEdge\install.bat`. Ele faz backup do FusionFix original, instala o `.asi` e
   acrescenta a seção `[LIBERTYEDGE]` ao `GTAIV.EFLC.FusionFix.ini` e instala os
   `frontend_menus.xml` do FusionFix com as novas opções de menu (os originais ficam no backup).
4. No jogo: **Gráficos → Anti-aliasing → SMAA+**.

Para desinstalar: `LibertyEdge\uninstall.bat` (restaura o `.asi` e o `.ini` do backup).

## Configuração (`plugins\GTAIV.EFLC.FusionFix.ini`)

```ini
[LIBERTYEDGE]
TextureThreshold = 0.10      ; maior = texturas mais nítidas
GeometryThreshold = 0.03     ; menor = mais AA em silhuetas, postes, fios
DepthEdgeSensitivity = 0.08  ; salto relativo de profundidade que conta como silhueta
SearchSteps = 32
SearchStepsDiagonal = 16
Downsample = 0               ; 1 = supersampling (ver acima)
```

`[POSTFX] EnablePreAlphaDepth = 1` (padrão do FusionFix) é necessário para a parte de profundidade.

## Como é construído

O repositório não copia o FusionFix: o GitHub Actions baixa o FusionFix na versão fixada
(`FUSIONFIX_REF` em `.github/workflows/build.yml`), aplica os patches de `patches/`, valida os
shaders com `fxc` e compila em Windows. O resultado é o artefato `LibertyEdge.zip`.

## Licença

GPL-3.0, como o FusionFix, do qual este projeto é derivado. SMAA © 2013 Jorge Jimenez et al. (MIT).
Não redistribui arquivos do jogo nem de outros mods.
