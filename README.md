# Liberty Edge — Resolution Unlock

Supersampling para **GTA IV: The Complete Edition** em placas que não têm DSR/VSR no driver
(ex.: **Intel Arc**). O jogo passa a renderizar acima da resolução do monitor e a imagem é reduzida
para a tela, o que diminui bastante o serrilhado.

> Supersampling for GTA IV on GPUs without a driver-level DSR/VSR (e.g. Intel Arc): adds resolutions above
> the monitor to the game's Video Mode menu and downsamples them properly to the screen. Standalone ASI.

## O que faz
- Acrescenta ao menu **Gráficos → Modo de vídeo** resoluções acima do monitor: 1,5×, 2× e 3×
  (num monitor 1080p: 2880×1620, 3840×2160, 5760×3240). A escolha fica salva pelo próprio jogo.
- Mantém a janela do tamanho do monitor e corrige a posição do mouse no menu e no mapa.
- Faz a redução para a tela com um filtro próprio: média em luz linear de todos os pixels renderizados,
  seguida de uma nitidez leve (estilo AMD CAS). Sem isso, o DXVK aproveita só 1 de cada 4 pixels em 2×.
- Corrige um problema do próprio jogo: ele lê teclado e mouse (DirectInput) mesmo com outro programa na
  frente e converte as teclas em caracteres, o que consome as teclas de acento do Windows. Com o jogo aberto,
  a acentuação (ABNT2 e outros layouts com teclas mortas) para de funcionar em todo lugar e o jogo continua
  reagindo depois do alt-tab. O mod faz o jogo ignorar teclado e mouse fora de primeiro plano (`KeyboardFix`).
- **Lista de mods instalados** dentro do jogo: aperte **F11** (`ModListKey`). Mostra os `.asi` (com versão),
  as pastas de `update` e as DLLs ao lado do `GTAIV.exe`.
- É um `.asi` independente: não substitui nem depende de uma versão específica do FusionFix.

## Instalação
1. Baixe `LibertyEdge.ResolutionUnlock.zip` da release **latest** (aba Releases).
2. Extraia na pasta do jogo (a pasta `plugins` do zip vai ao lado do `GTAIV.exe`). Precisa de um ASI
   loader; o FusionFix já instala um.
3. No jogo (aba Jogo do FusionFix): **Janela = Não** e **Janela sem borda = Sim**. O mod transforma a tela cheia
   exclusiva em janela sem borda do tamanho do monitor (`ForceWindowed`), o que evita travar ao trocar de
   resolução e deixa o alt-tab limpo.
4. **Gráficos → Modo de vídeo**: escolha uma resolução acima do monitor. Na Arc B580, 2880×1620 foi o
   melhor equilíbrio; 3840×2160 é mais nítido e mais pesado.

Para desinstalar, apague `plugins\LibertyEdge.ResolutionUnlock.asi` e volte o Modo de vídeo para a
resolução do monitor.

## Configuração (`plugins\LibertyEdge.ResolutionUnlock.ini`)
| Chave | Padrão | |
|---|---|---|
| `Scales` | `1.5, 2, 3` | múltiplos da resolução do monitor oferecidos no menu |
| `Filter` | `1` | redução com filtro próprio (0 = deixa para o DXVK/driver) |
| `Sharpen` | `0.3` | nitidez após a redução (0 desliga, até 1) |
| `FitWindow`, `MouseFix`, `ForceWindowed` | `1` | janela no tamanho do monitor, mouse, tela cheia → janela |
| `KeyboardFix` | `1` | jogo só lê teclado e mouse em primeiro plano (acentuação, alt-tab) |
| `ModListKey` | `0x7A` (F11) | tecla da lista de mods instalados (0 desliga) |

Diagnóstico em `LibertyEdge.ResolutionUnlock.log` (sessão atual) e `.prev.log` (sessão anterior), na pasta do
jogo. Se o jogo fechar com erro, `LibertyEdge.ResolutionUnlock.crash.log` registra em qual módulo (jogo, mod ou
driver) aconteceu.

## Mods recomendados junto
- [FusionFix](https://www.nexusmods.com/gta4/mods/716): base recomendada (ASI loader, Vulkan, correções).
- [Grass and Procedural Props Fix](https://www.nexusmods.com/gta4/mods/1230) (iammrmikeman): aumenta os limites de
  grama, vegetação e objetos procedurais, que somem em manchas no jogo original. Copie o `.asi` e o `.ini` para
  `plugins`; o autor recomenda Distância de detalhe = 100. Se aparecer objeto piscando, sumindo ou travamento,
  volte o `.ini` para os valores padrão e veja o `GTAIV.EFLC.ProceduralFixes.log`.

## Notas
- Testado em GTA IV CE 1.2.0.59 com FusionFix e a API gráfica Vulkan (DXVK), Intel Arc B580, monitor 1080p.
- O custo é o de renderizar na resolução escolhida: 2× por eixo = 4× os pixels.
- Histórico: este repositório começou como um conjunto de patches de anti-serrilhado sobre o FusionFix
  (SMAA+, fios). Isso foi abandonado; o que ficou é o supersampling, que foi o que funcionou. O código
  antigo continua no histórico do git.

## Build
GitHub Actions (Windows, MSVC x86): compila o shader com `fxc` e o `.asi` com MinHook, e publica a release `latest`.

## Licença
GPL-3.0. MinHook © Tsuda Kageyu (BSD-2-Clause).
