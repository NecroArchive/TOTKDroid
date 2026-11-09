// The equipped weapon, bow and shield, for the touch controls' icons (ui/touch_controls.mm) - BOTWRecompAndroid reads
// 1.8.2's uking::ui::PauseMenuDataMgr. TOTK keeps its inventory in its game data (gmd), each value under its name's
// Murmur3 hash, as its save does (progress.sav, 2026-11-03): Pouch.Weapon.EquipIndex (b80e3dbb), Pouch.Bow.EquipIndex
// (2cf18d2b), Pouch.Shield.EquipIndex (350ad90e) - an int array whose first is the equipped one's place, -1 none - and
// the weapons', bows' and shields' actor names (string64 arrays: 65efd0be, 791c4a0b, 273190f4; their names unknown).
// Where the game holds them while it runs isn't known yet: LARECOMP_SCAN_GMD=1 logs where those hashes are in its
// memory, once, 30 s after the touch controls first ask. Until then: none, the fixed icons stay.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../platform.h"
#include "../rt.h"

int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);

static const uint32_t gmd_hash[6] = {0xb80e3dbb, 0x2cf18d2b, 0x350ad90e, 0x65efd0be, 0x791c4a0b, 0x273190f4};

static void scan_gmd(void) {
    static uint64_t addr[4096], size[4096];
    int n = kernel_data_regions(addr, size, 4096), hits = 0;
    LOG("[gmd] scanning %d regions\n", n);
    for (int r = 0; r < n && hits < 200; r++) {
        for (uint64_t p = addr[r]; p + 4 <= addr[r] + size[r] && hits < 200; p += 4) {
            uint32_t v = *(volatile uint32_t *)(uintptr_t)p;
            int h = -1;
            for (int i = 0; i < 6; i++) if (v == gmd_hash[i]) h = i;
            if (h < 0) continue;
            hits++;
            uint64_t a = p - 32 < addr[r] ? addr[r] : p - 32, b = p + 64 > addr[r] + size[r] ? addr[r] + size[r] : p + 64;
            char hex[400];
            int o = 0;
            for (uint64_t q = a; q < b && o < (int)sizeof hex - 20; q += 8)
                o += snprintf(hex + o, sizeof hex - o, "%s%016llx", q == p - (p & 7) ? " [" : " ",
                              (unsigned long long)*(volatile uint64_t *)(uintptr_t)q);
            LOG("[gmd] %08x at %#llx (region %#llx +%#llx):%s\n", gmd_hash[h], (unsigned long long)p,
                (unsigned long long)addr[r], (unsigned long long)(p - addr[r]), hex);
        }
    }
    LOG("[gmd] scan done: %d hits\n", hits);
}

bool pouch_equipped(char out[3][64]) {
    static int scan = -1;
    static uint64_t first_ms;
    if (scan < 0) scan = getenv("LARECOMP_SCAN_GMD") != NULL;
    if (scan == 1) {
        uint64_t now = plat_now_ns() / 1000000;
        if (!first_ms) first_ms = now;
        if (now - first_ms > 30000) scan = 2, scan_gmd();
    }
    (void)out;
    return false;
}
