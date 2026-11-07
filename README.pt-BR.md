# TOTKDroid

[English](README.md) | **Português**

Um port nativo de *The Legend of Zelda: Tears of the Kingdom* (Nintendo Switch, versão **1.4.3**) para Android, em
beta. **Não é emulação**: o próprio código ARM64 do jogo roda direto na CPU do seu celular, o sistema do
Switch é substituído pelo runtime, e os gráficos (NVN) são traduzidos para Vulkan.

Feito sobre o motor do **[LAS Recomp](https://github.com/peiikai/LASRecomp)**, de peiikai (uma recompilação de
*Link's Awakening*), do seu porte para Android **[LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid)** e do
**[BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid)**, de onde este repositório saiu.

Antes chamado **TOTKRecompAndroid**: renomeado quando o código do jogo passou a rodar nativo e o build recompilado saiu
do app (30/10/2026). O endereço antigo do repositório redireciona para cá. O app mantém o id do Android
(`com.totkrecompandroid`), então atualiza por cima de uma cópia instalada, com os saves.

> [!WARNING]
> **Beta.** No celular (com o driver Turnip) um jogo novo ou um save vão do prólogo à caverna e à Grande Ilha Celestial;
> a maior parte do mapa ainda não foi jogada. Com Vídeo > Taxa de quadros 60, um POCO F7 Ultra frio segura 60 fps no
> prólogo e 54-57 no bosque da ilha; em sessões longas o calor do celular baixa isso para ~45. O plano e o estado de
> cada fase estão em [docs/TOTK-PORT.md](docs/TOTK-PORT.md). Versões: [Releases](https://github.com/mamcjr/TOTKDroid/releases).

## Plataforma

Somente Android (arm64, Vulkan). Windows, Linux ou macOS servem como máquina de desenvolvimento, com o SDK/NDK do
Android.

## Estado

- O app (tela inicial, configurações, controles de toque, controles físicos, mods, escolha do driver Vulkan) vem do
  BOTWRecompAndroid e compila.
- A 1.4.3 roda sobre o SDK 20.5.22 da Nintendo, a mesma linha do 20.5.6 do BOTW 1.8.2 (NVN 55): o que o motor aprendeu
  com o BOTW 1.8.2 vale. Os patches do jogo e muito do que se sabe dele vêm do
  [MacNativeGames](https://github.com/ly05010419/MacNativeGames), que roda exatamente esta versão nativa em Macs e
  iPhones.
- O código do jogo roda nativo na CPU do celular: configurar o jogo só copia os arquivos. A recompilação estática
  (228.238 funções do `main` e 40.516 do SDK, num `totkgame.so` de 224 MB) fica para desenvolvimento
  (`LARECOMP_NATIVE=0`): andando na caverna o frametime oscilava (desvio de 7,4 ms contra 0,6 no nativo) e o build no
  celular levava horas, então o app não traz mais o compilador.
- No celular (POCO F7 Ultra, driver Turnip): a tela título, jogo novo e saves; o prólogo, a caverna (as paredes e o
  fundo das cutscenes iluminados, como no Switch) e a Grande Ilha Celestial, com o HUD, as legendas e as vozes. A
  água, o mar de nuvens, a grama e a iluminação (sem os quadrados pretos) ficam como no Switch. O mundo anda pelo
  tempo real, então quadros acima de 16,7 ms não deixam o jogo em câmera lenta. As vozes, a música e o ambiente chegam
  à mixagem. Com o driver da própria Qualcomm também roda, mais devagar.
- Taxa de quadros: Vídeo > Taxa de quadros tem 30, Auto (prefere 60, cutscenes em 30) e 60. A resolução dinâmica do
  jogo fica desligada; a cena é 1600x896, escalada para a saída (FSR 1, GSR, SMAA / FXAA).
- Vídeo > Proporção > Desbloqueado: o mundo e os menus ocupam a tela larga (o HUD fica nos 16:9 do meio).
- Falta: a maior parte do mapa, 60 fps sustentados com o celular quente, o HUD nas bordas da tela no modo largo.

> [!IMPORTANT]
> Como o LAS Recomp, este projeto é feito quase inteiramente com IA (Claude, da Anthropic, pelo Claude Code), dirigido,
> testado e revisado por uma pessoa.

> [!NOTE]
> **Nenhum arquivo do jogo vem incluído.** É preciso o seu próprio dump, obtido legalmente, do *Tears of the Kingdom*
> com a atualização 1.4.3 aplicada (`exefs` e o `romfs`). Não serão dadas instruções de como obtê-lo.

## Compilando (desenvolvedores)

Com o SDK / NDK do Android e o seu dump em `Dump/0100F2C0115B6000/` (`exefs/` e `romfs/`, ou a imagem do RomFS
`romfs.bin`; a pasta fica fora do git), com Python e os pacotes `lz4`, `capstone` e `unicorn` (o
`tools/nsp_extract.py` desempacota NSPs e as title keys deles para o hactoolnet):

```bash
cd tools && python totk_dump.py                              # confere o dump (módulos, versão, romfs)
cd android/totkrecompandroid && ./gradlew assembleDebug      # o app
ROMFS_HOST=1 android/push_game.sh Dump/0100F2C0115B6000      # os arquivos do jogo para o celular
# opcional, só para desenvolvimento - o código recompilado (rode com LARECOMP_NATIVE=0):
# android/build_gamelib.sh Dump/0100F2C0115B6000/exefs       # ~20 min num PC, depois push_game.sh de novo
android/run_boot.sh 120                                      # inicia o jogo, log em android/out/boot.log
```

## Apoie o projeto

O TOTKDroid é um projeto gratuito e independente. Se você gosta dele e quer apoiar seu desenvolvimento, pode me
apoiar pelo GitHub, pelo Ko-fi ou pelo Buy Me a Coffee. Esse apoio me ajuda a dedicar tempo à correção de bugs, à
melhoria do desempenho e da compatibilidade e às próximas versões. O patrocínio é totalmente opcional: não libera
recursos exclusivos nem garante correções específicas. Relatos de bugs, testes em outros aparelhos e contribuições
também ajudam muito.

[![Patrocine no GitHub](https://img.shields.io/badge/Patrocine-no%20GitHub-EA4AAA?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/mamcjr)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-mamcjr-FF5E5B?logo=kofi&logoColor=white)](https://ko-fi.com/mamcjr)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-mamcjr-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/mamcjr)

## Créditos

- [peiikai](https://github.com/peiikai) pelo [LAS Recomp](https://github.com/peiikai/LASRecomp), o recompilador e o
  runtime de onde este projeto parte (MIT, ver [LICENSE](LICENSE)). O README original está em [docs/las/](docs/las/).
- [MacNativeGames](https://github.com/ly05010419/MacNativeGames) (MIT), que roda o Tears of the Kingdom 1.4.3 nativo em
  Macs e iPhones: os sítios de patch da 1.4.3 verificados por ele (a resolução fixa, o passo de quadro em tempo real, o
  relógio de pano e cabelo, o tamanho de renderização) e o que ele descobriu (o flag de early-Z e o arredondamento da
  altura de bloco do NVN, as leituras de profundidade e a comparação de sombra D16, o contador de visibilidade).
- [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp) (MIT), uma recompilação estática do Tears
  of the Kingdom 1.4.3 para Windows/Vulkan: o dimensionamento e o scissor de render passes sem anexos (luz ambiente do
  interior nas rochas da caverna), a ordem de canais dos formatos NVN empacotados de 16 bits (`RGBA4`/`RGB5`/`RGB5A1`/`RGB565`
  e `BGR565`/`BGR5`/`BGR5A1`/`A1BGR5`/`BGRX8`/`BGRA8` nos mapas de normais e mistura do terreno), a preservação de
  páginas já mapeadas em `MapPhysicalMemory` e os achados documentados da 1.4.3.
- [LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid), o seu porte para Android, e
  [BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid) (o README dele em [docs/botw/](docs/botw/)): o app,
  a compilação do código do jogo no celular, os controles de toque, os mods, a compatibilidade de drivers, os menus em
  português, o visual Sheikah e tudo o que o motor aprendeu com o SDK do BOTW 1.8.2.
- PKMSVRecompAndroid (um porte irmão, do *Pokémon Scarlet*, não publicado): as versões para um jogo de uma versão só, o
  program id lido do NPDM e os pares exclusivos de 16 bytes no recompilador.
- [Revivedx](https://github.com/Revivedx) pelo [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp) (MIT):
  as correções de kernel do SDK 20, o setjmp, o NVN 55.
- [Nycolazs](https://github.com/Nycolazs) pelo LASRecomp-Android-Plus (MIT): compatibilidade de drivers Vulkan.
- A decompilação [zeldaret/botw](https://github.com/zeldaret/botw) e o [open-ead/sead](https://github.com/open-ead/sead),
  pelo que se sabe do motor de onde o TOTK veio.
- Upscaling e anti-aliasing: [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) (MIT),
  [Qualcomm Snapdragon GSR](https://github.com/SnapdragonStudios/snapdragon-gsr) (BSD-3-Clause),
  [SMAA](https://github.com/iryoku/smaa) (MIT).
- Bibliotecas: [SDL](https://libsdl.org) (zlib), [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [libadrenotools](https://github.com/bylaws/libadrenotools) (BSD-2-Clause), [volk](https://github.com/zeux/volk) (MIT),
  [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT),
  [shaderc](https://github.com/google/shaderc) (Apache 2.0), [FFmpeg](https://ffmpeg.org) (LGPL),
  [LZ4](https://github.com/lz4/lz4) (BSD-2-Clause), [Capstone](https://www.capstone-engine.org) (BSD-3-Clause),
  [Unicorn](https://www.unicorn-engine.org) (GPLv2, compilado do código-fonte por `android/deps/build_unicorn.sh`),
  [Python](https://www.python.org) (PSF) e [Zig](https://ziglang.org) (MIT) para a compilação no celular.
- O driver Turnip do [Mesa](https://www.mesa3d.org) (MIT), que os jogadores carregam por conta própria.
- Fontes: [Baloo 2](https://github.com/EkType/Baloo2) (OFL), DejaVu Sans (licença Bitstream Vera). O robô do Android
  é reproduzido ou modificado a partir de trabalho criado e compartilhado pelo Google (CC BY 3.0).
- Feito com o [Claude Code](https://claude.com/claude-code) (Anthropic).

*The Legend of Zelda* e *Tears of the Kingdom* são marcas registradas da Nintendo. Este projeto não é afiliado à
Nintendo nem endossado por ela.
