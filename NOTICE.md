# Licenses of TOTKDroid

- **TOTKDroid's own code is MIT** ([LICENSE](LICENSE)), as are LAS Recomp's, LASRecompAndroid's and
  BOTWRecompAndroid's, which it builds on.
- **Except one folder, GPL-3.0-or-later:** [`runtime/nvn/vulkan/lsfg/`](runtime/nvn/vulkan/lsfg/LICENSE.md) holds Lossless
  Scaling frame generation (LSFG). It is a C port of [Eden](https://git.eden-emu.dev/eden-emu/eden)'s, which is based on
  [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk), taken from Swap2-Native.
  - Files: `lsfg.h`, `lsfg_chain.c`, `lsfg_dll.c`, `lsfg_pacer.c`, plus TOTKDroid's adapter `lsfg_host.h`. All carry
    `SPDX-License-Identifier: GPL-3.0-or-later`, with [COPYING](runtime/nvn/vulkan/lsfg/COPYING).
  - The glue that calls it, `runtime/nvn/vulkan/framegen.c`, is MIT.
- **An APK built with LSFG is GPL-3.0-or-later as a whole.** This is the default build (CMake `TOTK_LSFG`, Gradle
  property `totkLsfg`). It is a combined work of the MIT files and the GPL folder. Whoever passes such an APK on must
  offer its complete source: this repository at the commit of the release.
  - `./gradlew assembleRelease -PtotkLsfg=false` builds an APK without the folder, MIT as a whole. In that APK the setting
    says "not in this build".
- **Lossless Scaling's shaders are never included.** The app reads them at run time from the player's own copy of
  `Lossless.dll` (Lossless Scaling, proprietary, sold on Steam). The DLL is not part of this repository or of any APK.
  The app looks for it in `LSFG/`, `Download/`, `Documents/Emulation/bios/`, `Switch2/` and the root of the phone's
  storage, and in the app's data folder.
- **Third-party code** keeps its own license, listed in [README.md](README.md#credits).
