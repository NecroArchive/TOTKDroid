# License of this folder: GPL-3.0-or-later

The files here are the Lossless Scaling frame generation (LSFG) of [Swap2-Native](https://github.com/mamcjr) (its
`dm/gfx/vk_android/lsfg`): a C port of [Eden](https://git.eden-emu.dev/eden-emu/eden)'s
(`src/video_core/frame_gen/{lossless_dll,lsfg_translate}.cpp`, `src/video_core/renderer_vulkan/present/{frame_gen_pacer,lsfg_*}.cpp`),
which builds on [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk). They are licensed GPL-3.0-or-later, and so is this folder:
each file keeps its SPDX copyright lines (Eden Emulator Project 2026, lsfg-vk 2025). The full license text is [COPYING](COPYING).

TOTKDroid's own changes: `lsfg_host.h` (in place of Swap2-Native's renderer header `vka.h`: the device, its functions,
a memory type finder, a barrier and the log, from TOTKDroid's `vulkan/vk.h`) and the one `#include` in `lsfg_chain.c`
that names it.

The rest of TOTKDroid is MIT ([LICENSE](../../../../LICENSE)). The Android build links these files by default
(`TOTK_LSFG`, `android/totkrecompandroid/app/src/main/cpp/CMakeLists.txt`), and a `libmain.so` or APK built with them is
a combined work under GPL-3.0-or-later as a whole; see [NOTICE.md](../../../../NOTICE.md). Built with `-DTOTK_LSFG=OFF`
(Gradle `-PtotkLsfg=false`), none of this folder is in it. TOTKDroid's glue, `vulkan/framegen.c`, is MIT on its own.

The shaders are not here and never are: they are read at run time from the user's own copy of `Lossless.dll` (Lossless
Scaling, proprietary), which is not part of this repository or of any APK.

| File | From Eden (through Swap2-Native) |
|---|---|
| `lsfg_dll.c` | `frame_gen/lossless_dll.cpp`, `frame_gen/lsfg_translate.cpp` |
| `lsfg_pacer.c` | `present/frame_gen_pacer.cpp` |
| `lsfg_chain.c` | `present/lsfg_common.cpp`, `lsfg_chain.cpp`, `lsfg_mipmaps.cpp`, `lsfg_alpha.cpp`, `lsfg_beta.cpp`, `lsfg_gamma.cpp`, `lsfg_delta.cpp`, `lsfg_generate.cpp` |
| `lsfg.h` | the headers of those |
