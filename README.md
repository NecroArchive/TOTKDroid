# TOTKDroid

**English** | [Português](README.pt-BR.md)

A native port of *The Legend of Zelda: Tears of the Kingdom* (Nintendo Switch, version **1.4.3**) for Android, in
beta. This is **not emulation**: the game's own ARM64 code runs directly on your phone's CPU, the Switch's system
is replaced by the runtime, and its graphics (NVN) are translated to Vulkan.

It is built on the engine of **[LAS Recomp](https://github.com/peiikai/LASRecomp)** by peiikai (a recompilation of
*Link's Awakening*), its Android port **[LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid)** and
**[BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid)**, which this repository was forked from.

Formerly **TOTKRecompAndroid**: renamed once the game's code ran natively and the recompiled build left the app
(2026-10-30). The old repository address redirects here; the app keeps its Android id (`com.totkrecompandroid`), so
it updates over an installed copy with its saves.

> [!WARNING]
> **Beta.** On the phone (with the Turnip driver) a new game or a save plays through the prologue, the cave and the
> Great Sky Island; most of the map has not been played yet. With Video > Frame rate 60, a cool POCO F7 Ultra holds 60
> fps in the prologue and 54-57 in the island's woods; over long sessions its heat brings that down to ~45. The plan
> and the state of each phase are in [docs/TOTK-PORT.md](docs/TOTK-PORT.md) (in Portuguese).
> Releases: [Releases](https://github.com/mamcjr/TOTKDroid/releases).

## Supported platform

Android only (arm64, Vulkan). Windows, Linux or macOS may be used as development hosts with the Android SDK/NDK.

## Status

- The app (start screen, settings, touch controls, controllers, mods, Vulkan driver choice) comes from
  BOTWRecompAndroid and builds.
- 1.4.3 runs on Nintendo SDK 20.5.22, the line of BOTW 1.8.2's 20.5.6 (NVN 55): what the engine learned from BOTW 1.8.2
  applies. The game's patches and much of what is known about it come from
  [MacNativeGames](https://github.com/ly05010419/MacNativeGames), which runs this very build natively on Macs and iPhones.
- The game's code runs natively on the phone's CPU, so setting the game up only copies its files. The static
  recompilation (228,238 functions from `main` and 40,516 from the SDK, in a 224 MB `totkgame.so`) is kept for
  development (`LARECOMP_NATIVE=0`): walking in the cave it swung the frame time (sd 7.4 ms against 0.6 natively) and
  took hours to build on the phone, so the app no longer ships its builder.
- On the phone (POCO F7 Ultra, the Turnip driver): the title screen, a new game and saves; the prologue, the cave
  (its walls and the cutscene backgrounds lit, as on the Switch) and the Great Sky Island, with the HUD, subtitles and
  voices. Water, the sea of clouds, and the grass and lighting (no more black squares) match the Switch. The world
  steps by real time, so frames above 16.7 ms never slow the game down. Character voices, music and ambience reach the
  mix. With Qualcomm's own driver it plays too, more slowly.
- Frame rate: Video > Frame rate has 30, Auto (prefers 60, cutscenes held at 30) and 60. The game's own dynamic
  resolution is off; the scene is 1600x896, scaled to the output (FSR 1, GSR, SMAA / FXAA).
- Video > Aspect ratio > Unlocked: the world and the menus fill a wide screen (the HUD stays in the middle 16:9).
- Not done yet: most of the map, sustained 60 fps on a hot phone, the HUD at the screen's edges in wide mode.

> [!IMPORTANT]
> Like LAS Recomp, this project is made almost entirely with AI (Claude, by Anthropic, through Claude Code), directed,
> tested and reviewed by a human.

> [!NOTE]
> **No game files are included.** You need your own legally obtained dump of *Tears of the Kingdom* with the 1.4.3
> update applied (`exefs` and the `romfs`). Instructions on how to obtain it will not be provided.

## Building (developers)

With the Android SDK / NDK and your dump in `Dump/0100F2C0115B6000/` (`exefs/` and `romfs/`, or the RomFS image
`romfs.bin`; the folder is ignored by git), with Python and the `lz4`, `capstone` and `unicorn` packages
(`tools/nsp_extract.py` unpacks NSPs and their title keys for hactoolnet):

```bash
cd tools && python totk_dump.py                              # check the dump (modules, version, romfs)
cd android/totkrecompandroid && ./gradlew assembleDebug      # the app
ROMFS_HOST=1 android/push_game.sh Dump/0100F2C0115B6000      # the game files onto the phone
# optional, development only - the recompiled game code (run it with LARECOMP_NATIVE=0):
# android/build_gamelib.sh Dump/0100F2C0115B6000/exefs       # ~20 min on a PC, then push_game.sh again
android/run_boot.sh 120                                      # start the game, log to android/out/boot.log
```

## Support the project

TOTKDroid is a free, independent project. If you enjoy it and would like to support its development, you can sponsor
me on GitHub, Ko-fi, Buy Me a Coffee or via **Pix**. Your support helps me dedicate time to fixing bugs, improving performance and
compatibility, and working on future releases. Sponsorship is entirely optional: it does not unlock features or
guarantee specific fixes. Bug reports, device tests, and contributions are also greatly appreciated.

This month's goal is to pay for Claude Code Max ($100/month), the AI coding tool I use to work on my projects. The
badge below shows how much has come in this month, across all platforms and shared by BOTWRecompAndroid, TOTKDroid
and LASRecompAndroid; I update it by hand.

[![Claude Code Max goal](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2Fmamcjr%2FBOTWRecompAndroid%2Fmain%2F.github%2Fdonation-goal.json&query=%24.raised&prefix=%24&color=2EA043&logo=claude&logoColor=white&label=Claude%20Code%20Max%20goal&suffix=%20%2F%20%24100%20this%20month)](https://github.com/mamcjr/BOTWRecompAndroid/blob/main/.github/donation-goal.json)

[![Sponsor on GitHub](https://img.shields.io/badge/Sponsor-on%20GitHub-EA4AAA?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/mamcjr)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-mamcjr-FF5E5B?logo=kofi&logoColor=white)](https://ko-fi.com/mamcjr)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-mamcjr-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/mamcjr)
[![Pix](https://img.shields.io/badge/Pix-Random%20Key-32BCAD?logo=pix&logoColor=white)](#support-the-project)

> **Pix (Brazil only — random key):** `6c326b56-6caf-4c3a-bcf7-1fa8ad2abd16`

## Credits

- [peiikai](https://github.com/peiikai) for [LAS Recomp](https://github.com/peiikai/LASRecomp), the recompiler and
  runtime this project starts from (MIT, see [LICENSE](LICENSE)). Its original README is kept in [docs/las/](docs/las/).
- [MacNativeGames](https://github.com/ly05010419/MacNativeGames) (MIT), which runs Tears of the Kingdom 1.4.3 natively on
  Macs and iPhones: its verified 1.4.3 patch sites (the fixed resolution, the real-time frame step, the cloth and hair
  clock, the render size) and its findings (NVN's early-Z flag and block height rounding, depth reads and the D16
  shadow compare, the visibility counter).
- [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp) (MIT), a Windows/Vulkan static
  recompilation of Tears of the Kingdom 1.4.3: its attachment-less render pass sizing and scissor handling (interior
  ambient light on cave rocks), 16-bit packed NVN format component ordering (`RGBA4`/`RGB5`/`RGB5A1`/`RGB565` and
  `BGR565`/`BGR5`/`BGR5A1`/`A1BGR5`/`BGRX8`/`BGRA8` for terrain normal and blend maps), `MapPhysicalMemory` preservation
  of already-mapped pages, and its documented 1.4.3 findings.
- [LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid), its Android port, and
  [BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid) (its README in [docs/botw/](docs/botw/)): the app,
  the phone-side game code build, touch controls, mods, driver compatibility, the Portuguese menus, the Sheikah look and
  everything the engine learned from BOTW 1.8.2's SDK.
- PKMSVRecompAndroid (a sibling port of *Pokémon Scarlet*, not published): the version handling for a game with one
  version, the program id from the NPDM and the 16-byte exclusive pairs in the recompiler.
- [Revivedx](https://github.com/Revivedx) for [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp) (MIT):
  the SDK 20 kernel fixes, setjmp, NVN 55.
- [Nycolazs](https://github.com/Nycolazs) for LASRecomp-Android-Plus (MIT): Vulkan driver compatibility.
- The [zeldaret/botw](https://github.com/zeldaret/botw) decompilation and [open-ead/sead](https://github.com/open-ead/sead),
  for what is known about the engine TOTK grew from.
- Upscaling and anti-aliasing: [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) (MIT),
  [Qualcomm Snapdragon GSR](https://github.com/SnapdragonStudios/snapdragon-gsr) (BSD-3-Clause),
  [SMAA](https://github.com/iryoku/smaa) (MIT).
- Libraries: [SDL](https://libsdl.org) (zlib), [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [libadrenotools](https://github.com/bylaws/libadrenotools) (BSD-2-Clause), [volk](https://github.com/zeux/volk) (MIT),
  [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT),
  [shaderc](https://github.com/google/shaderc) (Apache 2.0), [FFmpeg](https://ffmpeg.org) (LGPL),
  [LZ4](https://github.com/lz4/lz4) (BSD-2-Clause), [Capstone](https://www.capstone-engine.org) (BSD-3-Clause),
  [Unicorn](https://www.unicorn-engine.org) (GPLv2, built from its source by `android/deps/build_unicorn.sh`),
  [Python](https://www.python.org) (PSF) and [Zig](https://ziglang.org) (MIT) for the phone-side build.
- [Mesa](https://www.mesa3d.org)'s Turnip driver (MIT), which players load themselves.
- Fonts: [Baloo 2](https://github.com/EkType/Baloo2) (OFL), DejaVu Sans (Bitstream Vera license). The Android robot
  is reproduced or modified from work created and shared by Google (CC BY 3.0).
- Made with [Claude Code](https://claude.com/claude-code) (Anthropic).

*The Legend of Zelda* and *Tears of the Kingdom* are trademarks of Nintendo. This project is not affiliated with or
endorsed by Nintendo.
