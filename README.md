# Liberty Edge — Resolution Unlock

Supersampling para **GTA IV: The Complete Edition** em placas que não têm DSR/VSR no driver
(ex.: **Intel Arc**). O jogo passa a renderizar acima da resolução do monitor e a imagem é reduzida
para a tela, o que diminui bastante o serrilhado.

> Supersampling for GTA IV on GPUs without a driver-level DSR/VSR (e.g. Intel Arc): adds resolutions above
> the monitor to the game's Video Mode menu and downsamples them properly to the screen. Standalone ASI.

## O que faz
- Acrescenta ao menu **Gráficos → Modo de vídeo** resoluções acima do monitor: por padrão 2× (num monitor
  1080p, 3840×2160); outras escalas pelo `Scales`. A escolha fica salva pelo próprio jogo.
- Mantém a janela do tamanho do monitor e corrige a posição do mouse no menu e no mapa.
- Faz a redução para a tela com um filtro próprio: média em luz linear de todos os pixels renderizados,
  seguida de uma nitidez leve (estilo AMD CAS). Sem isso, o DXVK aproveita só 1 de cada 4 pixels em 2×.
- Corrige um problema do próprio jogo: ele lê teclado e mouse (DirectInput) mesmo com outro programa na
  frente e converte as teclas em caracteres, o que consome as teclas de acento do Windows. Com o jogo aberto,
  a acentuação (ABNT2 e outros layouts com teclas mortas) para de funcionar em todo lugar e o jogo continua
  reagindo depois do alt-tab. O mod faz o jogo ignorar teclado e mouse fora de primeiro plano (`KeyboardFix`).
- **Lista de mods instalados** dentro do jogo: aperte **F11** (`ModListKey`). Mostra os `.asi` (com versão),
  as pastas de `update` e as DLLs ao lado do `GTAIV.exe`.
- Só age sobre o dispositivo Direct3D e a janela do próprio jogo. Overlays e ferramentas que criam um
  dispositivo próprio dentro do jogo são ignorados (antes da 1.8 isso prendia o mouse a parte da tela e
  crashava na troca de resolução).
- É um `.asi` independente: não substitui nem depende de uma versão específica do FusionFix.

## Instalação
1. Baixe `LibertyEdge.ResolutionUnlock.zip` da release **latest** (aba Releases).
2. Extraia na pasta do jogo (a pasta `plugins` do zip vai ao lado do `GTAIV.exe`). Precisa de um ASI
   loader; o FusionFix já instala um.
3. No jogo (aba Jogo do FusionFix): **Janela = Não** e **Janela sem borda = Sim**. O mod transforma a tela cheia
   exclusiva em janela sem borda do tamanho do monitor (`ForceWindowed`), o que evita travar ao trocar de
   resolução e deixa o alt-tab limpo.
4. **Gráficos → Modo de vídeo**: escolha uma resolução acima do monitor. **2× (3840×2160 num monitor 1080p) é
   a recomendada**: é o único fator em que cada 4 pixels viram 1, então fica nítido. 1,5× (2880×1620) e outras
   escalas não inteiras ficam mais moles que o próprio 1080p. 3× usa muita memória num jogo de 32 bits e pode
   crashar. Por isso o padrão é só `Scales = 2`; para ter as outras, use `Scales = 1.5, 2, 3`.

Para desinstalar, apague `plugins\LibertyEdge.ResolutionUnlock.asi` e volte o Modo de vídeo para a
resolução do monitor.

## Configuração (`plugins\LibertyEdge.ResolutionUnlock.ini`)
| Chave | Padrão | |
|---|---|---|
| `Scales` | `2` | múltiplos da resolução do monitor oferecidos no menu |
| `Filter` | `1` | redução com filtro próprio (0 = deixa para o DXVK/driver) |
| `Sharpen` | `0.3` | nitidez após a redução (0 desliga, até 1) |
| `FitWindow`, `MouseFix`, `ForceWindowed` | `1` | janela no tamanho do monitor, mouse, tela cheia → janela |
| `KeyboardFix` | `1` | jogo só lê teclado e mouse em primeiro plano (acentuação, alt-tab) |
| `ModListKey` | `0x7A` (F11) | tecla da lista de mods instalados (0 desliga) |
| `Log` | `0` | arquivos de diagnóstico na pasta do jogo |

Diagnóstico: com `Log = 1`, o mod grava na pasta do jogo `LibertyEdge.ResolutionUnlock.log` (sessão atual),
`.prev.log` (sessão anterior) e, se o jogo fechar com erro, `.crash.log` com o módulo (jogo, mod ou driver) onde
aconteceu. Por padrão não grava nada.

## Setup usado no desenvolvimento

A base é o **TJGM's Drag n' Drop Archive** ([tjgm.dev](https://tjgm.dev/resources/gtaiv/TJGM%27s-Drag-n%27-Drop-Archive/),
vídeo: [YouTube](https://www.youtube.com/watch?v=mxOyRadPzmI)), uma compilação vanilla+ simples de instalar, que
serviu de ponto de partida para o projeto. Sobre ela, o setup ficou assim:

| Mod | O que faz | Onde |
|---|---|---|
| [FusionFix](https://www.nexusmods.com/gta4/mods/716) (ThirteenAG e equipe) | correções, Vulkan (DXVK), opções gráficas, ASI loader | `plugins`, `update\GTAIV.EFLC.FusionFix` |
| [Console Visuals](https://github.com/Tomasak/Console-Visuals) | vegetação, cercas, roupas e animações como no console | `update\ConsoleVisuals` |
| [Various Fixes](https://www.nexusmods.com/gta4/mods/824) | correções de modelos e texturas do jogo | `update\Various Fixes` |
| [Higher Resolution Vehicle Pack](https://www.nexusmods.com/gta4/mods/282) | texturas de veículos em alta resolução | `update\Ash_HiRes_VehiclesPack` |
| [Higher Resolution Miscellaneous Pack](https://www.nexusmods.com/gta4/mods/357) | texturas diversas em alta resolução | `update\Ash_HiRes_Misc` |
| [Proper Grass](https://www.nexusmods.com/gta4/mods/1660) ([GitHub](https://github.com/paulopatine/GTAIV.ProperGrass), paulopatine) | grama desenhada por instancing: não some, não pisca, cobre áreas que o jogo deixava vazias, custa pouco | `plugins` |
| **FusionFix com setas, GPS e farol** (contribuição deste projeto, ver abaixo) | modos de seta, setas pelo GPS, ciclo do farol | `plugins`, `update` |
| **Liberty Edge Resolution Unlock** (este) | supersampling para placas sem DSR/VSR, teclado, lista de mods | `plugins` |

Testado em: GTA IV CE 1.2.0.59 (Steam), Intel Arc B580, Ryzen 7 8700F, monitor 1920×1080 @165 Hz, API gráfica
Vulkan, jogando em **3840×2160 reduzido para 1080p** a 60 FPS estáveis.

Configurações usadas: Distância de visão 35, Distância de detalhe 35, Densidade de veículos 50, limitador em 60.
Neste PC o jogo fica preso ao processador (5 a 7 mil chamadas de desenho por quadro): o FPS é o mesmo de 800×600 a
3840×2160, então o 4K sai de graça e os sliders de distância são o que realmente pesa.

O **Grass and Procedural Props Fix** (Nexus 1230) foi usado antes e saiu: o guia da Gillian o removeu por causar
crashes, e com limites altos ele pesava no processador e foi o principal suspeito de um travamento ao carregar missão. O Proper Grass
faz o mesmo papel; não use os dois juntos. A release `propergrass-1.2.1` deste repositório é uma compilação sem
alterações do código do autor (MIT), para quem não usa o Nexus.

## Contribuição ao FusionFix: setas, GPS e farol
Feito para este setup e enviado ao FusionFix
(setas e GPS: [PR #1652](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/pull/1652); farol:
[PR #1662](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/pull/1662)). Para testar já,
a release **fusionfix-turnindicators-test** traz o FusionFix 5.1.2 com tudo isso (`plugins` e `update`).

| Opção | Onde | O que faz |
|---|---|---|
| Turn Indicators | menu Controles | Off / Automatic / On (manual) / Automatic + manual; muda na hora |
| Auto Indicators | menu Controles | o que as setas automáticas seguem: Volante + GPS / Volante / GPS |
| `ManualTurnIndicators` | `.ini` `[TURNINDICATORS]` | legado: faz Automatic agir como On (manual) |
| `LeftIndicatorKey` / `RightIndicatorKey` | `.ini` `[TURNINDICATORS]` | teclas das setas manuais (`[` e `]`) |
| `CycleHeadlights` | `.ini` `[HEADLIGHTS]` | o comando de farol do jogo (H ou controle) passa por Automático → Alto → Baixo → Desligado |
| `ShowNotice` | `.ini` `[HEADLIGHTS]` | mostra o modo do farol na tela |

- **Setas manuais no controle:** toque em LB/RB liga a seta; os dois juntos, pisca-alerta. Com setas manuais, o
  jogo só recebe LB/RB (atirar, freio de mão) depois de 300 ms segurando, então o toque não atira nem freia.
- **GPS:** com waypoint ou rota de missão, a seta acende pouco antes de cada curva anunciada pela voz do GPS e
  apaga depois da curva. Lê a instrução com `GENERATE_DIRECTIONS` (1 direita, 2 esquerda) e o destino na lista de
  blips do radar (waypoint ou o blip com a flag de rota).
- **Farol:** o ciclo acompanha o próprio comando de farol do jogo, venha ele do teclado ou do controle; nenhuma
  tecla é lida ou substituída.

Não recomendados neste setup: RTX Remix (feito para NVIDIA), ENB/iCEnhancer (conflitam com Vulkan/FusionFix).

## Notas
- O custo é o de renderizar na resolução escolhida: 2× por eixo = 4× os pixels.
- Histórico: este repositório começou como um conjunto de patches de anti-serrilhado sobre o FusionFix
  (SMAA+, fios). Isso foi abandonado; o que ficou é o supersampling, que foi o que funcionou. O código
  antigo continua no histórico do git.

## Build
GitHub Actions (Windows, MSVC x86): compila o shader com `fxc` e o `.asi` com MinHook, e publica a release `latest`.

## Licença
GPL-3.0. MinHook © Tsuda Kageyu (BSD-2-Clause).
