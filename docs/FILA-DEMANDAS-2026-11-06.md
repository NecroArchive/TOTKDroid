# Fila de demandas do TOTKDroid - 06/11/2026

> Atualização de 08/11: os complementos de memória por arquivo (B1-B3), variáveis do driver, CRLF, giroscópio, áudio, regressão NVN e FPS intermediário foram integrados ao código junto com os ajustes de pouca RAM e pipelines. Ver [relatório do port](PORT-BOTW-2026-11-08.md). Build, regressões locais/ARM64 e verificações curtas de jogo no POCO passaram; ver [resultados e limites](TESTE-POCO-2026-11-08.md). Aparelhos reais de 4 GB e Mali/Xclipse continuam pendentes. As tabelas abaixo preservam o levantamento original, não o estado atualizado de cada item.

Cobre as 21 issues abertas (#3 a #26) e os commits do BOTWRecompAndroid de 03/11 21:00 até a 0.3.1-beta (04/11).
Os itens que precisam do POCO entram na vez em D:\Projetos\Jogos\Android\POCO-FILA.md, sempre com uma cópia do save.

## Estado de hoje

- **Commits só locais (sem push nem release):**
  - 41d1b30: cooldown da troca de equipamento.
  - 14e824e: TXD como textureGrad (#15).
  - bdece28: "Use them in place" sem Version.txt e com checagem rápida (#6, #16, #22).
- **Sem commit no working tree, desta sessão** (portado do BOTW em 04/11):
  - Memória do jogo por arquivo compartilhado (memfd; ed53b68 + 52a9132; opção `heap_file`, desligada por padrão).
  - Imagens-sombra BC (bdbe389).
  - Reabertura do rastreio de escrita (1f9c7be).
  - Write-back linear de 1 MB (e38e692).
  - No POCO em modo Mali simulado, a memória por arquivo levou o jogo de 10,6 para 38-45 fps e corrigiu a iluminação apagada e sem sombras.
- **Sem commit no mesmo working tree, de outras sessões:**
  - Controles touch com ícones: `touch_controls.mm`, `pouch.c`, partes de `settings.*`, i18n, `menu.mm`, `controls_ui.mm` e `CMakeLists.txt`.
  - Giroscópio: diagnóstico em `motion.c` e zona morta mínima de 8% em `input.c`.
  - **No commit, separar por trechos** (`git add -p`): `exec.c`, `launcher.mm`, `settings.*` e i18n são compartilhados.

## A. Release 0.2.2-beta: primeiro

| # | Demanda | Issues | O que fazer |
|---|---|---|---|
| A1 | Commitar o lote Mali desta sessão, separado do trabalho das outras sessões | #7 #9 #13 #17 #19 #20 | Ver "Estado de hoje": `git add -p` só nos trechos do memfd, BC, rastreio e write-back |
| A2 | Portar os complementos do memfd que vieram depois do port | os mesmos | Ver B1 a B3 |
| A3 | Dica de "~40 GB livres" é sobra do alfa: um Copy precisa de ~17 GB | #4 | Trocar o texto no `launcher.mm` e nos 11 i18n. **Bloqueia o release:** a resposta na #4 já prometeu a correção na próxima versão |
| A4 | Testar "Use them in place" pelo seletor de pastas com um romfs/ completo e ver o jogo abrir pelo `romfs.link` | #6 #16 #22 | No POCO, com acesso a todos os arquivos. Até hoje só a checagem foi testada, com uma pasta falsa |
| A5 | Push de 41d1b30, 14e824e, bdece28 e dos novos commits, e release 0.2.2-beta | - | Só o APK, assinado com a chave de release. Nunca desinstalar o app de debug |

## B. Mali: memória por arquivo e o que falta

Hoje toda Mali roda no modo de cópias: a CPU copia a memória do jogo para a GPU e rastreia escritas. Isso envia centenas de MB a alguns GB a cada 2 s e deixa os shaders lerem dados velhos. Daí vêm a lentidão e a luz que pisca/escurece.

| # | Demanda | Issues | O que se sabe | Próximo passo |
|---|---|---|---|---|
| B1 | **BOTW 2f7b906:** dma-buf quando o fd opaco não dá memória utilizável pela CPU | #13 #17 #19 #20 (Xclipse no BOTW #234) | No TOTK, sem isso, a opção cai em "heap placement failed" e volta às cópias | `vk.h` mem_dmabuf; `device.c` habilita VK_EXT_external_memory_dma_buf; `vk_heap_place` tenta OPAQUE_FD e depois DMA_BUF |
| B2 | **BOTW 1055b53, parte do log:** `[vk-cap] memory export` e o motivo de a memória por arquivo não ser usada | os mesmos | Sem isso, os logs das Mali não dizem por que falhou | `device.c` depois de `mem_fd`; `vk_heap_place` registra `memoryTypeBits` na falha |
| B3 | **BOTW 3a47fde:** mapeamentos de memória separados por dono | #17 #19 #20 #13 | Mostra se os ~62 mil mapeamentos são da janela rastreada ou do driver Mali | Trocar o laço de `/proc/self/maps` em `platform/android.c` |
| B4 | Tipo de memória escolhido na Mali: o coerente (0x7) não tem cache | Mali em geral | No POCO os três tipos dão ~18 GB/s; na Mali o 0x7 deve ser lento para a CPU ler | Ler a sonda no 1º log de Mali com a opção ligada; se for lento, testar `LARECOMP_VK_MEMFD_TYPE` com o tipo em cache (0xb) |
| B5 | Testar o memfd sem ponteiro para a memória do jogo (caminho do G57: Vulkan 1.1, alcance de 256 MB) | #20 | Sem buffer device address, os shaders acessam a memória por janelas (gwin) | Modo Mali simulado com `LARECOMP_VK_GMEM_BDA=0` |
| B6 | Trava de ~8,6 s por quadro no 1º salto da Ilha do Céu (0,1 fps) | #9 #13 | Começa logo depois de uma falta na tabela. Parece um shader preso em dado velho, com o driver se recuperando a cada quadro | Precisa de um save na Ilha do Céu. Repetir com o memfd; se continuar, medir o tempo de GPU de cada envio |
| B7 | Fundo cinza no inventário | #17 | Provavelmente a captura borrada da cena lida no modo de cópias | Conferir no POCO em modo Mali simulado, com e sem a memória por arquivo |
| B8 | Ligar a opção por padrão na Mali | todas as Mali | Testado só no POCO | Depois de 2-3 logs bons de Mali real |

**Depois do release:** pedir a rayanThur (#9/#13), EggNoRanTea (#17), lookingfor-tomba e cLinKz1603 (#20) e zorozurro83-web (#19) que liguem a opção e mandem o log. A #7 já foi corrigida na 0.2.1: fechar quando o autor confirmar.

## C. Arquivos do jogo

| # | Demanda | Issues | O que se sabe | Próximo passo |
|---|---|---|---|---|
| C1 (FEITO bdece28, sem release) | "Use them in place" recusava toda pasta do TOTK | #6 #16 #22 | Faltava Version.txt e a listagem pelo seletor levava 20-30 min | Release (A5). Na 0.2.1, o contorno é criar `romfs/System/Version.txt` com `1.4.3` |
| C2 | "Copy them" fica horas em "Procurando arquivos 0%" e depois copia devagar | #21, #16 e #22 (comentários) | Lista os 304 mil arquivos pelo seletor do Android e abre cada um por URI. `romfs_build(dir, ...)` já monta o romfs.bin pelo caminho | Com acesso a todos os arquivos: `nativeCheckRomfsDir` + nova JNI `nativeBuildRomfsDir` + `nativeImageLoose` para os vídeos. Mostrar a contagem enquanto lista no caminho antigo. Cronometrar no POCO |
| C3 | "No game files found" sem log | #25 (chinês: "não encontra a pasta do jogo"), #6 (izzat12312, NxDump) | Causa desconhecida. Talvez o NXDumpTool separe ExeFS e RomFS em pastas diferentes, ou falte o main.npdm | Pedir log, modo e árvore de pastas. `find()` passa a registrar as pastas que visitou e o que viu nelas. Com permissão, buscar pelo caminho |
| C4 | romfs.bin de um pacote de terceiros ("TOTKDroid-0.1.0-beta-pacote"): 120 texturas NoFile e depois SIGSEGV | #18 | Não é GPU. O arquivo está incompleto ou não bate com a versão | Pedir que gere a partir do próprio dump. Opcional: contar os NoFile e avisar no launcher que os arquivos parecem incompletos |

## D. Travamentos, shaders e entrada

| # | Demanda | Issues | O que se sabe | Próximo passo |
|---|---|---|---|---|
| D1 | Adreno 650 (SD 865: Retroid Pocket 5, Z Fold2): device lost ~12 s depois de abrir | #8 | Um usuário confirmou com a camada de validação e um patch: o tradutor usa ballot/vote em fragment shaders, mas o Turnip do 650 só aceita no compute | Ler `VkPhysicalDeviceSubgroupProperties.supportedStages`; em `translate.c` (:1938, :2215), `lanes` só no fragment quando houver suporte, senão o caminho de uma lane. Entra na chave do cache de shaders. Testar no POCO fingindo só compute. Levar também ao BOTW |
| D2 | `FS translate failed: r2p cc` | #20 (log da G57) | `translate.c:2421`: R2P com destino CC não implementado | Inverso do P2R.CC: bits 0-3 de (Ra >> shift) em ccz/ccs/cc. Levar também ao BOTW |
| D3 | Sem giroscópio com controle conectado | #23 | A zona morta mínima de 8% está no `input.c`, sem commit. A sessão do giroscópio viu que o movimento chega à câmera, mas a mira não se move | Commitar o `input.c` (separado). Portar o `motion_flat` do BOTW 1055b53 **depois** que a sessão do giroscópio commitar o `motion.c`. É candidato a explicar a mira parada |
| D4 | fps abaixo de 20 depois de abrir/fechar o inventário e de salvar | #24 (POCO F7, Adreno 825) | Sem log. Candidatos: a trava de clock (Auto preso em 30), texturas reenviadas depois do menu, ou o modo de cópias no driver Qualcomm | Pedir log e driver. Reproduzir no POCO comparando as linhas `[vk] t=` antes e depois |
| D5 | Engasgos no 8 Gen 3; chão preto no POCO F7 Pro | #14 #15 | Engasgos: compilação de pipelines na hora. Chão preto: desconhecido, talvez a sonda de ambiente escura | Pedir log (#14) e print com o local (#15). Estudar compilação em segundo plano ou um cache de pipelines pré-aquecido |
| D6 | Texturas piscando, pop-in, 10-20 fps num Adreno 740 (Xiaomi Pad 6S Pro) | #26 | O Automático só liga a memória na GPU no Adreno 8xx. No 740 provavelmente roda o modo de cópias | Pedir log. Sugerir "Game memory on the GPU: On". Avaliar incluir o Adreno 7xx com placed maps no Automático |

## E. Outros ports do BOTW

| # | Commit | Valor | O que fazer |
|---|---|---|---|
| E1 | 86d5f31: código do jogo lendo a página nula recebe zero e continua | médio | O TOTK no Android sempre roda nativo. `null_read()` em `platform/android.c` antes de `wt_fault`, trocando `botwgame` por `totkgame` |
| E2 | 29dea99: campo "Variáveis do driver" | médio | `driver_env` em settings, `device.c` (setenv antes do driver) e `launcher.mm` |
| E3 | 18ff625: 40/45 fps em telas de 120/90 Hz | médio | Precisa de trabalho próprio: o lado do jogo no BOTW é específico da 1.8.2. Ajudaria #14/#24 |
| E4 | 9e7c34e: CRLF no ini | baixo | Duas linhas em `settings.c` |
| E5 | 64c3273: áudio volta depois do segundo plano (patch do SDL para AAudio) | baixo | Copiar o patch para `android/deps` e reconstruir a libSDL3 |
| E6 | e4d8ca0 + cdb85c6: pacer com VK_GOOGLE_display_timing | baixo | O TOTK não passa pelo ramo da grade 30 fps do BOTW. Só vale se aparecer a alternância 50/16,7 ms |
| E7 | 4b032aa: o teste test_nvn_lifetime | baixo | Mesmo diff em `tools/test_nvn_lifetime.c` |
| - | 5c34fa1 (ADPF), 40dd2a1, 51e3f9a, f8a6131 | não se aplicam | O TOTK já mira 16,7 ms no ADPF. Os outros usam endereços e HUD do BOTW |

## F. Respostas (o dono posta)

**Podem ir agora (só pedem informação ou falam da política):**
- #14, #15, #18 (gerar o romfs.bin do próprio dump), #19, #24, #25, #26: pedir log.
- #3/#12: NSP recusado por causa do DMCA. Fechar a #12 como duplicata da #3.

**Só depois da 0.2.2:**
- #6/#16/#22 ("Use them in place" corrigido).
- #4 (fechar com o release).
- #7/#9/#13/#17/#19/#20 (ligar a memória por arquivo e mandar log).
- #23 e #8, se entrarem no release.
- #21: não prometer o Copy rápido antes de ele sair.

As minutas em inglês estão no resultado do levantamento. A resposta da #20 deve ir para cLinKz1603 (G615), não para o autor da #19.

## Ordem sugerida

1. **A1-A3 + B1-B3 + D2 + D1:** um lote para a 0.2.2. D1 e D2 também vão para o BOTW.
2. **POCO:**
   - "Use them in place" completo (A4).
   - Modo Mali com memfd, com e sem BDA (B5).
   - Inventário (B7).
   - D1 com só compute simulado.
3. **A5:** push e release. Depois, as respostas da seção F.
4. **C2:** Copy rápido. Depois C3 (diagnóstico do `find()`).
5. **B6:** trava da Ilha do Céu. Depende de um save lá, que o usuário vai fazer.
6. **E1-E5, D3** (depois da sessão do giroscópio), **D4-D6**, **B8**.
