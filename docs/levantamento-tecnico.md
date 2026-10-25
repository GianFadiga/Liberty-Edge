# Liberty Edge — Levantamento técnico de anti-serrilhado (Fase 1)

Data: 23/10/2026 · Status: análise concluída, **nenhum arquivo do jogo foi alterado**.

## 0. Inventário verificado na instalação

| Componente | Encontrado | Como verifiquei |
|---|---|---|
| GTAIV.exe | 1.2.0.59 (Steam / Complete Edition) | recurso de versão do executável + log do Setup Utility |
| Modpack | "Gillian's Modpack – 27.06.2026" | `update/update.txt` |
| FusionFix | **5.0.1 (commit 619f52d, 11/05/2026)** — a versão mais recente no GitHub é a 5.1.2 (08/10/2026) | recurso de versão do `.asi` |
| Seletor de API | `d3d9.dll` = proxy do FusionFix (`FusionFixGraphicsApiSwitch`); `d3d9.cfg` → `API = 1` | strings + cfg |
| Wrapper | `vulkan.dll` = **DXVK** (log: DXVK v3.1.1), instalado pelo GTAIVSetupUtility em 22/10 | log `GTAIV_d3d9.log` |
| dxvk.conf | `presentInterval=1`, `numBackBuffers=3`, `maxFrameLatency=1` (sem nada de AA) | arquivo |
| GPU vista pelo DXVK | Arc B580, driver 101.9150; o DXVK informa ao jogo "AMD RX 6700 XT" (`hideNvidiaGpu`, embutido) | log |
| ASI loader | `dinput8.dll` | arquivo |
| Outros ASI | ConsoleSelectMenuIV, XboxRainDroplets, LibertyCityPlates | `plugins/` |
| Mods do modpack relevantes p/ AA | 8 Console Visuals (**ConsoleFences**, **ConsoleVegetation**), 9 Project Glass, FusionShaders (`update/common/shaders`, incl. `gta_cutout_fence.fxc`, `gta_trees_extended`, `gta_wire`) | `update/` |
| **DLSS-IV (Nexus 1430)** | **não está instalado** (nenhum DLSSIV.cfg nem DLL dele) | busca na pasta |

Configuração gráfica atual do FusionFix (`GTAIV.EFLC.FusionFix.cfg`), decodificada pelo código da v5.0.1:
- `Antialiasing=7` → **SMAA** (no enum da 5.0.1: 5 = Off, 6 = FXAA, 7 = SMAA)
- `Definition=1`, `MotionBlur=1`, `DepthOfField=7` (Low), `TreeAlpha=7` (PC+), `ShadowFilter=5` (CHSS), `AmbientOcclusion=1`, `ToneMapping=1`
- `.ini`: `[EXPERIMENTAL] ReflectionMSAAQuality = 0` (desligado)

## (a) Como o AA funciona hoje

**FusionFix 5.0.1 — `source/postfx.ixx`, recursos `SMAA.hlsl` e `FXAA3_11.h`:**
- O FusionFix intercepta o *draw* do pós-processamento do jogo (`hbDrawPrimitivePostFX`). A ordem é: SSAO → stipple filter → DOF → sunshafts → **postfx do jogo (bloom, motion blur, tonemap)** → saída para `FullScreenTex_temp2` → **AA** → backbuffer.
- Como o AA roda dentro desse hook, **HUD, radar, textos e menus são desenhados depois**. Por construção, o AA não deveria tocar na interface (isso precisa ser confirmado nas capturas).
- **SMAA:** SMAA 1x clássico em 3 passes (detecção de bordas → pesos → mistura), em `ps_3_0`, com:
  - detecção de bordas **por luma**, em imagem LDR já com tonemap;
  - `SMAA_THRESHOLD 0.05` (nível "Ultra"), `MAX_SEARCH_STEPS 16`, `DIAG 8`, `CORNER_ROUNDING 25`;
  - **sem predicação por profundidade** (`SMAA_PREDICATION 0`) e **sem reprojeção temporal** (`SMAA_REPROJECTION 0`), embora o código-fonte original do SMAA suporte as duas;
  - mistura final em sRGB.
  - Os shaders são compilados em tempo de execução a partir do recurso; se a compilação falhar, ele usa a versão pré-compilada.
- **FXAA:** FXAA 3.11 Quality preset 12, `subpix 0.25`, `edge 0.125`, `edgeMin 0.0312`.
- **Histórico:** em dezembro de 2023 entrou um `antialiasing.ixx` com opções de FXAA/SMAA/**SSAA 2x** (que aumentava os RTs HDR). Esse arquivo foi removido em julho de 2024 (commit 71b1a50), e o AA passou para o `postfx.ixx`. Não há MSAA de cena no código atual. Entre a 5.0.1 e a 5.1.2 não houve mudança no AA.
- **MSAA existente:** apenas o experimental `ReflectionMSAAQuality` (2/4/8), que aplica MSAA aos mapas de reflexão (exterior, interior, água, espelho), como no Xbox 360. A cena principal (G-buffer deferred) não recebe MSAA.
- Outros pontos que afetam serrilhado: a vegetação e as cercas usam alpha-test/stipple (screen-door), suavizado pelo `stipple_filter_ps` (`[SRF] StippleFilter`, padrão 1). O `TreeAlpha` controla o multiplicador de alpha das árvores.

**DLSS-IV (Nexus 1430) — só fontes secundárias; a página do Nexus não abriu:**
- Autor: ChunkLeChuck. Exige o FusionFix instalado antes. Usa DXVK. Oferece DLSS Quality/Balanced/**DLAA**, FSR Quality/Ultra Performance/**FSR Native AA** (FSR 4 apenas em RX 7000/9000; nas outras placas, FSR 3.1.5), HDR com a curva de gama do Xbox 360 do FusionFix, e um `DLSSIV.cfg`.
- As matérias dizem que DLSS/FSR usam **vetores de movimento por objeto**. Isso contradiz diretamente a hipótese de que "TAA é inviável": alguém já obteve os dados temporais. XeSS não é mencionado.
- Na Arc B580, **DLSS/DLAA não estão disponíveis**. O candidato é o **FSR 3.1 Native AA**, que segundo as matérias funciona em qualquer GPU. Isso não foi verificado.

## (b) Diagnóstico

1. O SMAA atual já está no limiar mais agressivo (0.05). O que resta de serrilhado está, sobretudo, onde um AA espacial de 1 amostra não consegue chegar:
   - **geometria subpixel** (fios, cabos, grades e galhos distantes): a borda some e reaparece entre quadros, e o SMAA só suaviza o que foi rasterizado;
   - **cintilação temporal**: nada no pipeline acumula informação entre quadros;
   - **alpha-test/stipple** (cercas, folhagem): o padrão de pixels muda com o movimento, e o SMAA pode reforçar o padrão do dither em vez de suavizá-lo;
   - **bordas de baixo contraste de luma** (cores diferentes com luma parecida): a detecção por luma sem predicação deixa essas bordas passarem.
2. O AA roda **depois** do bloom, do motion blur e do DOF. Em movimento, o motion blur mascara parte do problema, e a avaliação precisa levar isso em conta.
3. **Hipótese a testar:** reflexões serrilhadas (água, carros, vitrines) podem ser uma parte do incômodo que o `ReflectionMSAAQuality` já resolve, a custo baixo.
4. Conclusão: outro filtro espacial dá, no melhor caso, ganho marginal sobre o SMAA Ultra (predicação, detecção por cor). O ganho grande contra cintilação exige **informação temporal**, que o DLSS-IV afirma já fornecer.

## (c) Opções comparadas

| Opção | Ataca | Prós | Contras / riscos | Veredito |
|---|---|---|---|---|
| SMAA do FF (atual) | bordas estáticas | já integrado, HUD seguro | não resolve subpixel nem cintilação | linha de base |
| FXAA do FF | bordas | barato | borra texturas | referência |
| `ReflectionMSAAQuality=4` | reflexões | só um ajuste no ini; reversível | experimental; custo a medir | **testar já** |
| **DLSS-IV com FSR 3.1 Native AA** | cintilação, subpixel | já existe e é temporal, com MV por objeto (alegado) | compatibilidade com Arc/DXVK/FF 5.0.1 não verificada; ghosting; HUD? | **avaliar primeiro** |
| Fork do FF: SMAA com predicação por profundidade + parâmetros no ini | bordas de baixo contraste | mudança pequena, mesmo ponto de integração, HUD seguro | ganho marginal; o fork é sobrescrito em atualizações | protótipo nº 1 se o DLSS-IV falhar |
| Estabilizador temporal leve (sem jitter, só câmera) | cintilação parcial | dados existem: profundidade + matrizes atual/anterior (o motion blur do jogo já usa) | sem MV por objeto há ghosting em carros e pedestres; vidro e partículas não ficam no depth; sem jitter não recupera subpixel | fase 2, só com plano de rejeição |
| ReShade (SMAA/TAA) | bordas | fácil | roda depois do HUD e o borra; sem MV confiáveis | não recomendado |
| MSAA forçado (driver/DXVK) | geometria | — | o renderizador deferred não resolve o MSAA; o FF não implementa MSAA de cena | descartado sem prova |
| SSAA/DSR | tudo | — | fora das restrições; o FF já removeu o SSAA | descartado |

Detalhes do estabilizador temporal, caso se chegue à fase 2:
- **Dados:** profundidade do jogo e `PreAlphaDepthCopyRT` do FF; matrizes view-projection atual e anterior (constantes do shader de motion blur); histórico em um RT extra.
- **Objetos móveis:** não há vetor por objeto. Mitigação: clamp de vizinhança em YCoCg, rejeição por diferença de profundidade e peso de histórico baixo (cerca de 0.8–0.9).
- **Transparências:** máscara onde `depth_final ≠ PreAlphaDepth` (vidro, água, partículas). Nessas áreas, histórico zero.
- **HUD:** fica fora do caminho, porque roda no mesmo hook, antes da interface.

## (d) Plano de testes reproduzível

**Mantenha constante durante todos os testes:** DXVK e `dxvk.conf`, `API=1`, 1920×1080 em borderless, limite de FPS, todas as outras opções do FF (registre o `.cfg` e o `.ini` de cada rodada). Faça backup de `plugins/` e dos `.cfg`/`.ini` antes de começar.

**Matriz de configurações:**
- C0 AA Off (`Antialiasing=5`)
- C1 FXAA (6)
- C2 SMAA (7)
- C3 SMAA + `ReflectionMSAAQuality=4`
- C4 DLSS-IV com FSR Native AA, se funcionar na Arc
- C5 protótipo

Rode cada configuração com MotionBlur **desligado** (medição) e ligado (uso real).

**Cenas** (use uma save diferente para cada uma, sempre à mesma hora e com o mesmo clima; carregue a save e capture nos primeiros 30 s):
1. **Cabos e fios:** cabos da Broker Bridge contra o céu, e fios/trilhos elevados em Algonquin.
2. **Cercas e grades:** uma cerca de alambrado em Bohan ou Dukes (com ConsoleFences ativo).
3. **Folhagem:** Middle Park, árvores contra o céu.
4. **Distância:** o skyline de Algonquin visto da orla de Dukes.
5. **Reflexos:** água do porto e lataria de carro sob luz de poste.
6. **Noite com chuva:** bordas de alto contraste + XboxRainDroplets.
7. **Interface:** radar e HUD visíveis, legenda de diálogo, telefone aberto, mapa do menu de pausa (TransparentMapMenu=1) e cutscene com letterbox.
8. **Movimento:** giro lento e fixo da câmera na cena 1 (mesmo gesto do mouse, cerca de 5 s) e um trecho fixo de rua dirigindo com postes.

**Captura:**
- Use screenshot PNG sem perdas (Steam com cópia não comprimida, ou OBS).
- Para movimento, grave em OBS sem perdas a 60 fps.
- Desligue qualquer overlay durante as capturas.
- Compare recortes a 200%, sempre na mesma região.

**Desempenho:** use PresentMon ou CapFrameX (funcionam com o DXVK/Vulkan) e registre média, 1% low e o frametime de uma corrida de 60 s na cena 8, repetida 3 vezes por configuração. Para conferência rápida, use `DXVK_HUD=fps,frametimes`, só fora das capturas.

**Critérios de avaliação:**
- escadas visíveis;
- cintilação (em vídeo, por avaliação lado a lado);
- nitidez das texturas;
- artefatos: halos, ghosting, rastros, dither destacado;
- HUD, radar e textos intactos;
- custo em ms.

## (e) Riscos de conflito

- **DLSS-IV × DXVK do modpack:** se o DLSS-IV usar o seu próprio DXVK ou `vulkan.dll`, ele sobrescreve o que o Setup Utility instalou. O Setup Utility, se rodado de novo, pode desfazer a troca. Faça backup de `vulkan.dll`, `d3d9.dll`, `d3d9.cfg` e `dxvk.conf`.
- **Versão do FF:** você usa a 5.0.1 e a atual é a 5.1.2. O DLSS-IV pode exigir outra versão. Qualquer fork do FF é sobrescrito numa atualização do próprio FF ou do Setup Utility ("Latest FusionFix is present").
- **`hideNvidiaGpu`:** o jogo vê uma AMD. Não deve afetar o FSR, mas pode confundir a detecção de recursos de mods.
- **Alpha/stipple:** ConsoleFences, ConsoleVegetation, `TreeAlpha` e o StippleFilter mudam a aparência de cercas e folhas. Não mude essas opções no meio dos testes.
- **XboxRainDroplets:** é uma sobreposição de tela. Verifique se fica antes ou depois do AA e se borra.
- **HDR do DLSS-IV:** muda o tonemap e invalida a comparação com C0–C3. Teste com HDR desligado.
- **ReShade:** se for usado, roda depois do HUD.
- **Licença:** o FF é GPL-3.0. Um fork pode ser distribuído, desde que publique o código.

## Próximo passo (código só depois da sua aprovação)

1. Teste C3 (`ReflectionMSAAQuality=4`): é só uma linha no ini e é reversível.
2. Instale o DLSS-IV numa cópia de backup e teste C4 com FSR Native AA.
3. Se o C4 não funcionar na Arc ou trouxer artefatos inaceitáveis: protótipo nº 1, um fork do FF **tag v5.0.1**.
   - Novas chaves no ini: `[ANTIALIASING] SMAAThreshold`, `SMAAPredication` (0/1, usa a profundidade), `SMAAEdgeMode` (luma/cor). Elas são passadas como `D3DXMACRO` na compilação já existente.
   - Por padrão, o comportamento é idêntico ao atual. O menu continua com Off/FXAA/SMAA.
   - Para reverter, basta restaurar o `.asi` original.

## Dados que ainda faltam
- Página e arquivos do **DLSS-IV** (o Nexus bloqueou o acesso):
  - versão;
  - README/descrição;
  - lista de arquivos do zip;
  - `DLSSIV.cfg`;
  - a versão do FusionFix exigida;
  - se substitui `vulkan.dll`/`d3d9.dll`;
  - suporte declarado a Intel/XeSS;
  - link para o código-fonte, se houver.
- A issue de fevereiro de 2026 do FusionFix sobre AA não foi relida por mim (o acesso à API do GitHub foi bloqueado). As afirmações sobre TAA e supersampling continuam vindo do resumo do projeto. O que confirmei no código: o SSAA existiu e foi removido, e não há TAA.

## Fase 2 — alterações aplicadas (23/10/2026)
- Backup: `GTAIV\_LibertyEdge\backup_original_20261023\` (asi/ini/cfg do FF, d3d9.dll, d3d9.cfg, vulkan.dll, dxvk.conf, dinput8.dll + SHA256).
- **SMAA+**: patch só no recurso RCDATA 102 (SMAA.hlsl) do FusionFix 5.0.1, com o mesmo tamanho (58350 bytes). O FF compila esse recurso em runtime com D3DX9_43; se a compilação falhar, ele volta sozinho para o shader pré-compilado original.
  - Detecção por cor (SMAAColorEdgeDetectionPS).
  - Predicação por salto relativo de profundidade (`w=min(d,1-d)`, `rel=|Δw|/max(w)`, limiar 0.08), lendo s1 = PreAlphaDepthCopy (R32F) que o FF deixa ligado no passe de AA.
  - Limiares: 0.06 sem borda de profundidade e 0.024 com borda.
  - Busca de 32/16 passos.
- **DEBUG**: máscara de bordas na tela (vermelho/verde = bordas SMAA, azul = bordas de profundidade); confirma se o patch compilou e se a profundidade chega ao shader.
- Scripts `.bat` em `_LibertyEdge\` para alternar original/SMAA+/DEBUG, ligar e desligar ReflectionMSAA=4, restaurar tudo e ver o status (identifica a variante ativa pelo hash).
- Estado deixado: DEBUG ativo, ReflectionMSAA=0, menu em SMAA.
- Não validado offline: não havia compilador HLSL SM3 no ambiente. O primeiro teste no jogo é a validação.

### Teste 1 (23/10, 18:37) e v2
- O DEBUG v1 funcionou: o shader compilou em runtime, e HUD e radar ficaram intactos. O azul apareceu nos fios contra o céu, então a profundidade chega ao shader. O custo de FPS foi imperceptível (a 60 fps travado, GPU em cerca de 25%).
- Problema encontrado: a detecção por cor com limiar 0.06 marcou o grão das texturas (chão, tapete, papel de parede) como borda.
- v2: limiar de 0.10 em textura e 0.03 em borda de profundidade (SCALE 2.0, STRENGTH 0.7). A v1 ficou como "FORTE".
- Novo DEBUG mostra cor e profundidade sobrepostas. Também há um DEBUG_original (luma 0.05, regras do SMAA do FF) para comparar a cobertura de bordas.

### Teste 2 (23/10, 19:08): v2, DEBUG original e FORTE
- Pelos debugs: o SMAA original já marca muito grão de textura (tapete, papel de parede). A v2 marca menos grão e acrescenta as bordas de profundidade (azul e roxo nas silhuetas). Fios contra o céu noturno aparecem só em azul: há borda de profundidade, mas o contraste de cor fica abaixo do limiar, então o SMAA não os trata.
- Durante o dia, os fios aparecem **tracejados** (geometria menor que 1 px). Os braços de semáforo e os postes têm 1–2 px. Isso é o limite físico de qualquer SMAA 1x: não há como reconstruir pixels que nunca foram rasterizados.
- Achado no FusionShaders (gta_wire.fxc.xml, submódulo a851405 = FF 5.0.1):
  - O VS forward `draw` (VS0) já tem a técnica do Xbox: engrossa o fio com a distância (raio += 0.002·dist ao longo da normal) e faz fade por cobertura (`Fade_Thickness`, c210, `o6.z = sat(largura_ndc·320)`).
  - Os VS **deferred** (VS4–VS7), usados no PC, **não têm** isso. Por isso os fios quebram.
  - O stipple do PS3 deferred mais o `stipple_filter` do FF permitiriam emular a cobertura.
  - Protótipo possível: "Wire AA" (expandir para pelo menos 1 px, com cobertura por dither), compilado com o RageShaderEditor no Windows.
- Ganho maior para postes e cintilação: só com AA temporal (DLSS-IV com FSR Native AA).

### Fase 3 (23/10, noite): override dos fios e DLSS-IV
- `update/common/shaders/db/gta_wire.sps` (de 2023, origem fora do repositório do FF; provavelmente veio do modpack) muda o `__rage_drawbucket` dos fios de 1 (vanilla, em `common/`) para 0. Hipótese: com isso os fios saem do caminho forward (VS0, com engrossamento por distância e fade de cobertura) e vão para o deferred (VS4, sem nada disso), o que explica os fios tracejados. Para testar: `10_fios_modo_original.bat` e `11_fios_modo_modpack.bat` (rename reversível; backup guardado).
- O DLSS-IV 1.0.9 (2026-10-04) foi analisado pelos arquivos que você baixou:
  - Licença MIT. Faz vetores de movimento por objeto redesenhando os draws, inclusive dos skinned, e tem um "dedither" do stipple do FusionShaders.
  - Exige o FusionFix com API em Vulkan. O `vulkan.dll` dele é idêntico ao instalado (DXVK 3.1.1).
  - Na Arc fica com FSR 3.1 (FSR 4 só em RX 7000/9000). `quality=0` = native AA, em 1080p sem upscaling.
  - Recomenda deixar o AA do FusionFix em Off.
  - Também mexe em threads e afinidade de CPU (`threaded`, `unpin`, `cpu_place`) e em latência (`responsiveness=4`).
- Instalação: pasta `DLSSIV\` ao lado do exe; `plugins\DLSS-IV.cfg` e `plugins\DLSS-IV.asi.desativado`. Para alternar: `12_DLSSIV_LIGAR.bat` (também põe o AA do FF em Off) e `13_DLSSIV_DESLIGAR.bat` (volta o AA para SMAA).
- O Wire AA via shader (VS4 deferred com expansão + stipple) fica em espera até sair o resultado do teste do override.

### Teste 3 (23/10, 19:30): fios em modo original e DLSS-IV ligado
- Log do DLSS-IV: está saudável.
  - Backend FSR 3.1.5 (vendor 0x8086, NGX não testado), em D3D12 na B580 com SM 6.8, modo native AA 1920x1080→1920x1080.
  - O dedither encontrou as tags do FusionShaders. Vsync e o limite de 60 fps do FF estão ativos.
  - Houve um único "SLOW injection flush" de 156 ms.
- Pelo print diurno com DLSS-IV: braços de semáforo, escadas de incêndio e andaimes ficaram lisos, e os fios aparecem contínuos e finos. Falta um A/B no mesmo lugar e em movimento.
- No print noturno com os fios em modo original (sem DLSS-IV), os fios ficaram quase invisíveis. Também falta o A/B no mesmo lugar.
- Downsampling 4K→1080p na Arc: a Intel não tem equivalente a DSR/VSR. Um moderador da Intel confirmou isso em jul/2025. O `custom` do DLSS-IV só aceita 34–99% (abaixo do nativo). Não existe caminho nativo; o que sobra é o monitor aceitar sinal 4K (CRU), o que é incerto.

## Configuração aprovada (23/10/2026, 19:35)
- **DLSS-IV 1.0.9 ligado:** FSR 3.1.5 em Native AA a 1920x1080 (`quality=0`, `upscaler=-1`, `sharpness=0`, `hdr=0`; `threaded`, `unpin` e `cpu_place` = 1; `responsiveness=4`). O log da sessão terminou com 0 warnings e 0 falhas; houve 2 flushes lentos (156 e 58 ms).
- **FusionFix 5.0.1:**
  - AA do menu em **Off** (`Antialiasing=5`). Graphics API em Vulkan (`d3d9.cfg API=1`).
  - O `.asi` ativo é o SMAA+ v2 (04bf4d88), inerte enquanto o AA estiver em Off.
  - `ReflectionMSAAQuality=0`, `EnablePreAlphaDepth=1`.
- **DXVK 3.1.1:** o `vulkan.dll` é idêntico ao que vem no DLSS-IV. O `dxvk.conf` não foi alterado (vsync, 3 buffers, latência 1).
- **Fios:** modo original do jogo, com o override `gta_wire.sps` renomeado para `.desativado`.
- **Reversão:** pelos scripts em `_LibertyEdge\`; backup completo em `backup_original_20261023\`.

## 25/10: DLSS-IV removido
- O usuário relatou muito mais travadas que antes. O log de 25/10 (cerca de 13 min, 47 mil quadros) registra 6 "SLOW injection flush" entre 51 e 166 ms. Eles vêm do transporte entre o jogo e o processo auxiliar D3D12 do FSR; não houve warnings.
- Remoção: `DLSSIV\`, `plugins\DLSS-IV.asi`/`.cfg` e os logs foram movidos para `_LibertyEdge\DLSS-IV\removido_20261025\`. O `vulkan.dll` não mudou, porque era idêntico ao do FF.
- O AA do FF voltou para SMAA (`Antialiasing=7`), com a variante SMAA+ v2 ativa. Os fios ficaram no modo original.
- Caso queira tentar de novo no futuro: `threaded=0` e `cpu_place=0` no cfg. As travadas registradas parecem vir do transporte entre processos, então podem persistir.

## 25/10: FusionFix atualizado de 5.0.1 para 5.1.2 (instalação seletiva)
- Comparação em três vias (zip 5.0.1 × instalado × zip 5.1.2, 156 arquivos):
  - 123 arquivos são iguais nas duas versões, **incluindo todos os shaders**.
  - 23 mudam no 5.1.2: d3d9.dll, os 4 .img do FF, frontend_menus.xml (×3), textos .gxt e algumas texturas.
  - 6 foram personalizados pelo modpack e não mudam entre as versões. Ficaram intocados: carcols.dat e visualsettings.dat do TBoGT, visualsettings.dat comum, timecyc.dat do TBoGT, playerped.rpf do TLAD e update.txt.
  - dinput8.dll e vulkan.dll já estavam nas versões do 5.1.2.
- `.ini`: foi usado o do 5.1.2, mantendo os valores do modpack (DeathMusic, VehicleBudget, ExtendedLimits, Corona*). As chaves DrunkDriving* saíram no 5.1.2.
- `.cfg`: o 5.1.2 renumerou os enums do menu sem migrar valores antigos (o clamp mudaria escolhas). Os valores foram traduzidos para manter as mesmas escolhas:
  - Antialiasing 7→2
  - DepthOfField 7→2
  - TreeLighting e TreeAlpha 7→1
  - ScreenFilter 5→0
  - Buttons 6→1
  - ShadowFilter 5→2
  - FpsLimitPreset 7→5
- O recurso SMAA.hlsl é idêntico e o bloco de AA no postfx não mudou. As variantes SMAA+ foram regeradas para o 5.1.2 (SMAA+ v2 = 66be3145).
- Reversão: backup em `backup_pre_ff512_20261025\` e snapshot em `estado_ff512\`. Para alternar: `14_FusionFix_voltar_5.0.1.bat` e `15_FusionFix_ir_5.1.2.bat`. Os scripts de variante leem `versao_ff.txt`.
