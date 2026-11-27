**Beta 0.5.0.** **amiibo** com um leitor virtual, o **HUD nas bordas** da tela com a proporção Livre, menos **cenário piscando** e "cortinas", a opção **Esperar pelos shaders**, a **memória do jogo na GPU** ligada no Adreno 7xx, e menos pixels desenhados com a saída abaixo de 900 linhas.

> **Nenhum arquivo do jogo vem junto.** Você precisa do seu próprio dump de *Tears of the Kingdom* com a atualização **1.4.3** aplicada (`exefs` e `romfs`, ou o `romfs` como um único `romfs.bin`).

Atualiza por cima das versões anteriores sem apagar os saves. **Não desinstale o app antes**: isso apaga os saves.

## O que mudou

- **amiibo** (do BOTWRecomp):
  - Um leitor virtual de amiibo, com 36 figuras, as de *Tears of the Kingdom* entre elas. Não usa chaves nem dumps de amiibo.
  - Em *Controles > amiibo*: escolha a figura, toque em **Encostar o amiibo** quando o jogo procurar um, e em **Tirar o amiibo** depois.
  - Com o poder de amiibo do Purah Pad selecionado, tocar no ícone do poder (layout de ícones) ou apertar L (layout clássico) abre o menu já na escolha da figura.
  - Testado no POCO F7 Ultra: "Link - Twilight Princess" foi lido e deu os seus itens.
- **HUD nas bordas da tela (proporção Livre):** o minimapa, os corações e o resto do HUD ficavam presos na faixa 16:9 do meio de uma tela larga. Agora, jogando no mundo, eles vão para as bordas; o que fica no meio (avisos, o nome do lugar) continua no meio. As fileiras de corações ficam juntas, mesmo com 40 corações. Menus, conversas e telas de carregamento ficam como antes.
- **Menos cenário piscando e "cortinas":** as texturas que o jogo preenche na CPU (as lineares) agora são checadas a cada quadro, não a cada 8. Antes, uma camada podia ser montada com conteúdo de até 7 quadros atrás (do BOTWRecomp).
- **Vídeo > Esperar pelos shaders** (nova, desligada): um desenho espera o seu shader ficar pronto em vez de ser pulado. Nada pisca nem aparece do nada, mas o jogo para um instante quando monta um shader novo. No POCO, o cenário, a grama e as sombras pararam de piscar com ela ligada.
- **Memória do jogo na GPU, Automático no Adreno 7xx** ([#84](https://github.com/mamcjr/TOTKDroid/pull/84), de HallanBR): antes o Automático só ligava no Adreno 8xx. Num Adreno 750 (POCO F7 Pro) nas Ilhas do Céu, foi de ~15 para 27-30 fps a 720p, com a imagem igual. Continua desligado abaixo de ~7 GB de RAM.
- **Resolução de saída abaixo de 900 linhas** ([#86](https://github.com/mamcjr/TOTKDroid/pull/86), de HallanBR): com 1x, a cena era desenhada em 900 linhas só para ser reduzida. Agora ela é desenhada no tamanho da imagem final. Num POCO F7 Pro a 720p: o mesmo fps, a GPU de 76,5 para 72,5% de uso e o celular de 5,4 para 4,9 W. A vegetação fica um pouco mais suave, como escolher *Igual à tela*. Com 900 linhas ou mais, nada muda.
- **Menus** (do BOTWRecomp): as listas rolam com o dedo e continuam deslizando depois de um toque rápido; as listas de escolha são mais altas e a barra de rolagem mais larga. As configurações do overlay em *Mostrar FPS* ficam dentro de **Configurações do overlay** (mostrar / esconder).
- **Menos travamentos por falta de memória:** um pipeline que falha por falta de memória libera os que não estão em uso e é montado de novo 5 s depois.
- **Mali:**
  - Do ramo Mali do Savage Wind Recomp (de StonedEdge), ligados só na Mali: alvos de cor inteiros de 16 e 8 bits guardam os bits baixos, como no hardware da NVIDIA; as barreiras entre passes seguram só o trabalho de fragmento quando dá; alvos de cor que um passe nunca desenha ficam fora dele. Na Mali simulada no POCO: 20,6 fps contra 18,6, com a imagem igual.
  - No modo de cópia, a GPU devolve para a memória do jogo só as palavras que mudou.
  - O heap em blocos de AHardwareBuffer (opcional) agora abre o arquivo certo do buffer num POCO X7 Pro (Mali-G720) e checa com mais rigor se a GPU vê o que a CPU escreve. Onde não vê, o jogo fica no modo de cópia.
  - O overlay completo mostra o uso e o clock da GPU onde o app consegue ler.
- **Variáveis do driver ([#88](https://github.com/mamcjr/TOTKDroid/issues/88)):** as notas da 0.4.0 mostravam as variáveis separadas por vírgula, e assim a segunda nunca valia. Agora uma vírgula antes de outro `NOME=` também separa. Uma vírgula dentro de um valor (`TU_DEBUG=nolrz,sysmem`) continua como está.
- **Outras:**
  - O aviso de "nenhum quadro terminou por 5 s" não aparece mais logo depois de voltar à tela inicial.
  - Os render passes clássicos (drivers sem dynamic rendering) ficam numa tabela que cresce, em vez de uma lista de 512 que, cheia, criava um render pass novo a cada chamada.
  - Pré-aquecimento de pipelines (do Propinbo): existe, mas vem desligado no TOTK, porque não reduziu os desenhos pulados ao entrar no mundo.

Não testados num celular: a correção do aviso de 5 s, e as mudanças para Mali numa Mali de verdade (foram testadas simulando Mali num Adreno 830).

Ainda aberto: no save 100%, perto da cabana, a sombra ainda faz anéis na grama e quadrados escuros que piscam.

**Licença:** com a geração de quadros, este APK é distribuído sob a **GPL-3.0-or-later** (a pasta `runtime/nvn/vulkan/lsfg/` vem do Swap2-Native/Eden). O resto do código continua MIT. Veja `NOTICE.md`.

> Como o LAS Recomp, este projeto é feito quase todo com IA (Claude, da Anthropic, pelo Claude Code), dirigido, testado e revisado por uma pessoa.

---

**Beta 0.5.0.** **amiibo** through a virtual reader, the **HUD at the screen's edges** with the Unlocked aspect ratio, less **flickering scenery** and fewer "curtains", a **Wait for shaders** option, **game memory on the GPU** turned on for Adreno 7xx, and fewer pixels drawn when the output is under 900 lines.

> **No game files are included.** You need your own dump of *Tears of the Kingdom* with update **1.4.3** applied (`exefs` and `romfs`, or the `romfs` as a single `romfs.bin`).

Installs over earlier versions and keeps your saves. **Don't uninstall the app first**: that deletes the saves.

## What changed

- **amiibo** (from BOTWRecomp):
  - A virtual amiibo reader with 36 figures, the *Tears of the Kingdom* ones among them. It uses no keys and no amiibo dumps.
  - In *Controls > amiibo*: pick the figure, tap **Touch the amiibo** when the game looks for one, and **Lift the amiibo** afterwards.
  - With the Purah Pad's amiibo ability selected, tapping the ability's icon (icon layout) or pressing L (classic layout) opens the menu right on the figure picker.
  - Tested on the POCO F7 Ultra: "Link - Twilight Princess" was read and gave its items.
- **HUD at the screen's edges (Unlocked aspect ratio):** the minimap, the hearts and the rest of the HUD stayed inside the middle 16:9 strip of a wide screen. Now, while playing in the world, they go out to the edges; what sits in the middle (prompts, the location name) stays in the middle. The heart rows stay together, even with 40 hearts. Menus, talks and loading screens are as before.
- **Less flickering scenery and fewer "curtains":** textures the game fills on the CPU (the linear ones) are now checked every frame, not every 8th. Before, a layer could be built from contents up to 7 frames old (from BOTWRecomp).
- **Video > Wait for shaders** (new, off): a draw waits for its shader to be ready instead of being skipped. Nothing flickers or pops in, but the game stops for a moment when it builds a new shader. On the POCO, the scenery, grass and shadows stopped flickering with it on.
- **Game memory on the GPU, Automatic on Adreno 7xx** ([#84](https://github.com/mamcjr/TOTKDroid/pull/84), by HallanBR): Automatic used to turn it on only on Adreno 8xx. On an Adreno 750 (POCO F7 Pro) on the Sky Islands it went from ~15 to 27-30 fps at 720p, with the same picture. It still stays off below ~7 GB of RAM.
- **Output resolution under 900 lines** ([#86](https://github.com/mamcjr/TOTKDroid/pull/86), by HallanBR): with 1x, the scene was drawn at 900 lines only to be scaled down. Now it's drawn at the final image's size. On a POCO F7 Pro at 720p: the same fps, the GPU from 76.5 to 72.5% busy and the phone from 5.4 to 4.9 W. Foliage is a little softer, the same as choosing *Match display*. At 900 lines or more nothing changes.
- **Menus** (from BOTWRecomp): lists scroll with a finger and keep gliding after a flick; choice lists are taller and the scrollbar wider. The overlay settings under *Show FPS* are folded into **Overlay settings** (show / hide).
- **Fewer out-of-memory failures:** a pipeline that fails out of memory frees the unused ones and is built again 5 s later.
- **Mali:**
  - From Savage Wind Recomp's Mali branch (by StonedEdge), on Mali only: 16- and 8-bit integer colour targets keep the low bits, as NVIDIA's hardware does; barriers between passes hold back only fragment work where they can; colour targets a pass never draws into are left out of it. On the simulated Mali on the POCO: 20.6 fps against 18.6, with the same picture.
  - In copy mode, the GPU writes back to the game's memory only the words it changed.
  - The Android hardware buffer block heap (optional) now opens the right file of the buffer on a POCO X7 Pro (Mali-G720) and checks more strictly whether the GPU sees the CPU's writes. Where it doesn't, the game stays in copy mode.
  - The full overlay shows the GPU's load and clock where the app can read them.
- **Driver variables ([#88](https://github.com/mamcjr/TOTKDroid/issues/88)):** the 0.4.0 notes showed the variables separated by a comma, and that way the second one never took effect. Now a comma before another `NAME=` separates too. A comma inside a value (`TU_DEBUG=nolrz,sysmem`) stays as it is.
- **Other:**
  - The "no frame finished for 5 s" report no longer shows right after going back to the start screen.
  - Classic render passes (drivers without dynamic rendering) are kept in a table that grows, instead of a 512-entry list that, once full, made a new render pass on every call.
  - Pipeline prewarming (from Propinbo): it's there, but off in TOTK, since it didn't reduce the draws skipped on entering the world.

Not tested on a phone: the 5 s report fix, and the Mali changes on a real Mali (they were tested by simulating Mali on an Adreno 830).

Still open: in the 100% save, near the hut, the shadow still draws rings on the grass and dark squares that flicker.

**License:** with frame generation, this APK is distributed under **GPL-3.0-or-later** (the `runtime/nvn/vulkan/lsfg/` folder comes from Swap2-Native/Eden). The rest of the code stays MIT. See `NOTICE.md`.

> Like LAS Recomp, this project is made almost entirely with AI (Claude, by Anthropic, through Claude Code), directed, tested and reviewed by a person.
