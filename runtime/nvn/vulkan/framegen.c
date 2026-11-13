// Frame generation in the present (Lossless Scaling's LSFG: lsfg/, GPL-3.0-or-later, ported from Eden through
// Swap2-Native - this file is TOTKDroid's own glue, MIT, after Swap2-Native's vka_framegen.c). vk_present_now hands each
// game frame here before it shows it: the game's image is copied into the chain's input, the shared passes run, and the
// frames between the previous game frame and this one are written to images of their own, recorded into the frame's
// command buffer ahead of the window's draws. The present then shows each of them (through the same upscaling and with
// the overlay drawn again: menu_render_again), held for an equal share of the game's frame interval, before the game's.
// Off by default (g_settings.lsfg); the user's own Lossless.dll is never in the APK: looked for on the phone's storage
// (LSFG/, Download/, Documents/Emulation/bios, Switch2/, the storage's root, the app's data folder), or named by
// LARECOMP_LSFG_DLL. Without TOTK_LSFG (a build without the GPL folder) the setting reports "not in this build".
// LARECOMP_LSFG=0/1, LARECOMP_LSFG_MULT=2..4, LARECOMP_LSFG_FLOW=0 (auto) / 25..100: over the settings (tests).
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../paths.h"
#include "../../settings.h"
#include "vk.h"
#ifdef TOTK_LSFG
#include "lsfg/lsfg.h"
#endif

static char g_status[256] = "off";
static int g_state;  // 1 generating, 2 on and waiting, 0 off, -1 unavailable (the overlay: ui/perf_overlay.mm)
const char *vk_fg_status(void) { return g_status; }  // (the menu's line under the setting)
int vk_fg_state(void) { return g_state; }
static void status(const char *s) {
    g_state = !strncmp(s, "generating", 10) ? 1 : !strncmp(s, "on", 2) ? 2 : !strcmp(s, "off") ? 0 : -1;
    if (strcmp(g_status, s)) LOG("[lsfg] %s\n", s);
    snprintf(g_status, sizeof g_status, "%s", s);
}

static int env_int(const char *name, int def) {
    const char *e = getenv(name);
    return e && *e ? atoi(e) : def;
}
static bool fg_on(void) { return env_int("LARECOMP_LSFG", g_settings.lsfg ? 1 : 0) != 0; }

#ifndef TOTK_LSFG
int vk_fg_frame(TexBackend *b, double *hold, TexBackend *gen[3]) {
    (void)b; (void)gen;
    *hold = 0;
    status(fg_on() ? "not in this build (built without TOTK_LSFG)" : "off");
    return 0;
}
bool vk_fg_built(void) { return false; }
#else
bool vk_fg_built(void) { return true; }

static struct {
    int state;  // 0 not tried, 1 the shaders loaded, -1 unavailable (said in g_status)
    lsfg_modules mods;
    lsfg_chain *chain;
    VkExtent2D ext;
    int flow;
    TexBackend out[LSFG_MAX_GENERATIONS];
    VkImageView out_view[LSFG_MAX_GENERATIONS];
    lsfg_pacer pacer;
    uint64_t count, last_ns;
    uint32_t warm_streak;
    double interval;  // the game's frame interval, smoothed (s)
} F;

static void unavailable(const char *why) {
    F.state = -1;
    status(why);
}

// What the shaders use: 16-bit floats, storage images of the extended formats and without a format, null descriptors
// (device.c turns them on where the driver has them), and storage images of the chain's formats
static bool device_ok(void) {
    VkPhysicalDeviceFeatures f;
    vkGetPhysicalDeviceFeatures(g_vk.phys, &f);
    const char *miss = !g_vk.shader_float16 ? "shaderFloat16"
                     : !f.shaderStorageImageExtendedFormats ? "shaderStorageImageExtendedFormats"
                     : !f.shaderStorageImageWriteWithoutFormat ? "shaderStorageImageWriteWithoutFormat"
                     : !g_vk.null_desc ? "null descriptors (VK_EXT_robustness2)" : NULL;
    const VkFormat fmts[3] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
    for (int i = 0; i < 3 && !miss; i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g_vk.phys, fmts[i], &fp);
        if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
            !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            miss = "storage images of R8, RGBA8 or RGBA16F";
    }
    if (miss) {
        char b[160];
        snprintf(b, sizeof b, "unavailable: the GPU lacks %s", miss);
        unavailable(b);
    }
    return !miss;
}

static bool is_dll(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    char mz[2] = {0};
    bool ok = fread(mz, 1, 2, f) == 2 && mz[0] == 'M' && mz[1] == 'Z';
    fseek(f, 0, SEEK_END);
    ok = ok && ftell(f) > 100000;
    fclose(f);
    return ok;
}

// The user's Lossless.dll: LARECOMP_LSFG_DLL, else the first of the usual places that has one (the access to all files
// reaches the storage's folders; the app's data folder, Android/data/com.totkrecompandroid/files, always)
static bool find_dll(char *out, size_t n) {
    const char *e = getenv("LARECOMP_LSFG_DLL");
    if (e && *e) { snprintf(out, n, "%s", e); return is_dll(out); }
    static const char *const known[] = {"/storage/emulated/0/LSFG/Lossless.dll", "/storage/emulated/0/Download/Lossless.dll",
                                        "/storage/emulated/0/Documents/Emulation/bios/Lossless.dll",
                                        "/storage/emulated/0/Switch2/Lossless.dll", "/storage/emulated/0/Lossless.dll"};
    snprintf(out, n, "%s/Lossless.dll", rt_data_root());
    if (is_dll(out)) return true;
    for (size_t i = 0; i < sizeof known / sizeof *known; i++) {
        snprintf(out, n, "%s", known[i]);
        if (is_dll(out)) return true;
    }
    return false;
}

static bool load(void) {
    if (F.state) return F.state > 0;
    if (!device_ok()) return false;
    char path[512];
    if (!find_dll(path, sizeof path)) {
        char b[256];
        snprintf(b, sizeof b, "unavailable: no Lossless.dll (put yours in LSFG/ or Download/ on the phone's storage, or in %s)",
                 rt_data_root());
        unavailable(b);
        return false;
    }
    uint64_t t0 = SDL_GetTicksNS();
    int r = lsfg_dll_load(path, &F.mods);
    if (r) {
        char b[256];
        snprintf(b, sizeof b, "unavailable: %s: %s", path, lsfg_status_name(r));
        unavailable(b);
        return false;
    }
    LOG("[lsfg] %d shaders from %s (%.1f ms)\n", F.mods.n, path, (SDL_GetTicksNS() - t0) / 1e6);
    F.state = 1;
    return true;
}

static void out_free(int i) {
    TexBackend *t = &F.out[i];
    if (F.out_view[i]) vkDestroyImageView(g_vk.dev, F.out_view[i], NULL);
    F.out_view[i] = VK_NULL_HANDLE;
    if (t->image) {
        vk_views_forget(t);
        vmaDestroyImage(g_vk.vma, t->image, t->mem);
    }
    memset(t, 0, sizeof *t);
}

static void teardown(void) {
    if (!F.chain && !F.out[0].image) return;
    vk_wait_ready();  // (the GPU done with the chain and the outputs)
    vkDeviceWaitIdle(g_vk.dev);
    lsfg_chain_free(F.chain);
    F.chain = NULL;
    for (int i = 0; i < LSFG_MAX_GENERATIONS; i++) out_free(i);
    lsfg_pacer_reset(&F.pacer);
    F.count = 0, F.warm_streak = 0;
}

// An output as a texture the present draws as it draws the game's (vk_present_draw: upscaling and all)
static bool out_new(VkExtent2D e, int i) {
    TexBackend *t = &F.out[i];
    memset(t, 0, sizeof *t);
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {e.width, e.height, 1}, .mipLevels = 1,
                            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(g_vk.vma, &ci, &ai, &t->image, &t->mem, NULL) != VK_SUCCESS) {
        LOG("[lsfg] output image %ux%u: failed\n", e.width, e.height);
        return false;
    }
    t->format = VK_FORMAT_R8G8B8A8_UNORM, t->base_type = VK_IMAGE_VIEW_TYPE_2D, t->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    t->width = e.width, t->height = e.height, t->depth = t->layers = t->levels = t->samples = 1;
    t->gpu_owned = t->uploaded = true;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = t->image,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(g_vk.dev, &vi, NULL, &F.out_view[i]) != VK_SUCCESS) return false;
    return true;
}

static bool rebuild(VkExtent2D e, int flow) {
    teardown();
    VkImage imgs[LSFG_MAX_GENERATIONS];
    for (int i = 0; i < LSFG_MAX_GENERATIONS; i++) {
        if (!out_new(e, i)) { teardown(); return false; }
        imgs[i] = F.out[i].image;
    }
    uint64_t t0 = SDL_GetTicksNS();
    F.chain = lsfg_chain_new(&F.mods, e, flow ? flow / 100.0f : 1.0f, imgs, F.out_view);  // (outputs put in GENERAL by it)
    if (!F.chain) { teardown(); return false; }
    F.ext = e, F.flow = flow;
    LOG("[lsfg] chain for %ux%u (flow %d%%) made in %.1f ms\n", e.width, e.height, flow ? flow : 100,
        (SDL_GetTicksNS() - t0) / 1e6);
    return true;
}

// The panel's refresh period (s), 0 when unknown
static double refresh_s(void) {
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    return m && m->refresh_rate > 1 ? 1.0 / m->refresh_rate : 0;
}

// The game's image b about to be shown: the generated frames before it (gen, at most 3; their count returned) and how
// long each image of this game frame should stay (*hold, s). The work is recorded into the frame's command buffer.
// Render thread (vk_present_now).
int vk_fg_frame(TexBackend *b, double *hold, TexBackend *gen[3]) {
    *hold = 0;
    if (!fg_on()) {
        if (F.chain) { teardown(); LOG("[lsfg] off\n"); }
        if (F.state >= 0) status("off");
        return 0;
    }
    if (!load()) return 0;
    uint64_t now = SDL_GetTicksNS();
    if (F.last_ns) {  // (the game's frame interval: what the generated frames share out)
        double dt = (now - F.last_ns) / 1e9;
        if (dt > 0.25) dt = 0.25;
        F.interval = F.interval > 0 ? F.interval * 0.9 + dt * 0.1 : dt;
    }
    F.last_ns = now;
    if (!b || !b->image || b->aspect != VK_IMAGE_ASPECT_COLOR_BIT || b->samples != 1) return 0;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, b->format, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
        static int said;
        if (!said++) LOG("[lsfg] the game's image (format %d) can't be copied into the chain: not generating\n", b->format);
        status("on, but the game's image can't be read: not generating");
        return 0;
    }
    int mult = env_int("LARECOMP_LSFG_MULT", g_settings.lsfg_mult), flow = env_int("LARECOMP_LSFG_FLOW", g_settings.lsfg_flow);
    if (mult < 2) mult = 2;
    if (mult > 4) mult = 4;
    if (flow && flow < 25) flow = 25;
    if (flow > 100) flow = 100;
    VkExtent2D e = {b->width, b->height};
    if (!F.chain || F.ext.width != e.width || F.ext.height != e.height || F.flow != flow) {
        if (!rebuild(e, flow)) { unavailable("unavailable: the frame generation chain could not be made (see [lsfg] lines)"); return 0; }
    }
    double tnow = now / 1e9;
    lsfg_plan plan = lsfg_pacer_plan(&F.pacer, LSFG_MAX_GENERATIONS, mult, 0, tnow);
    uint64_t count = F.count++;
    int warm = plan.warm && count + 1 >= 2;
    F.warm_streak = warm ? F.warm_streak + 1 : 0;
    size_t ng = warm && F.warm_streak >= 2 ? plan.generations : 0;
    if (ng > LSFG_MAX_GENERATIONS) ng = LSFG_MAX_GENERATIONS;
    // (one image a refresh: no more generated frames than the refreshes a game frame leaves room for)
    double r = refresh_s(), period = F.interval > 0 ? F.interval : 1.0 / 30;
    if (ng && r > 0) {
        double n = period / r;  // (30 fps on 120 Hz: 4; on 60 Hz: 2)
        size_t fit = n < 1.9 ? 0 : (size_t)(n + 0.1) - 1;
        if (ng > fit) ng = fit, F.pacer.issued = ng;
    }
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);  // (the game's writes to its image)
    VkImageBlit bl = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)e.width, (int32_t)e.height, 1}},
                      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)e.width, (int32_t)e.height, 1}}};
    vkCmdBlitImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, lsfg_chain_input(F.chain, count), VK_IMAGE_LAYOUT_GENERAL, 1, &bl,
                   VK_FILTER_NEAREST);
    if (warm) lsfg_chain_shared(F.chain, cb, count);
    for (size_t g = 0; g < ng; g++) lsfg_chain_generate(F.chain, cb, count, ng, g);
    vk_full_barrier(cb);  // (the outputs, for the window's draws after this)
    for (size_t g = 0; g < ng; g++) gen[g] = &F.out[g];
    if (ng) *hold = period / (double)(ng + 1);
    char s[96];
    if (ng) snprintf(s, sizeof s, "generating: %zu frame%s per game frame", ng, ng > 1 ? "s" : "");
    else snprintf(s, sizeof s, r > 0 && period / r < 1.9 ? "on: the display has no refresh left for a generated frame"
                                                         : "on, waiting for a steady frame rate");
    status(s);
    return (int)ng;
}
#endif
#endif
