// Running the game's own ARM64 code on the phone's CPU (LARECOMP_NATIVE=1, a prototype: docs/EXECUCAO-NATIVA.md).
//
// The modules are loaded and relocated at their guest addresses as always (loader.c); guest addresses are host
// addresses, so the game's code can run there as it is. Only what can't run outside the Switch is rewritten, in place:
//   svc #n              -> a stub (in a patch area after the module) that calls native_svc_gate: the registers go into
//                          the thread's Cpu, rt_svc runs on the host stack, the registers come back
//   mrs/msr tpidr_el0   -> the guest's TPIDR_EL0 in its Cpu (bionic's TLS stays in the real register)
//   mrs tpidrro_el0     -> the guest's TPIDRRO_EL0 (its ThreadLocalRegion) from its Cpu
//   mrs cntpct_el0      -> mrs cntvct_el0 (the virtual counter: readable by apps; 19.2 MHz on Snapdragon, as the Switch)
// x18 is left alone: the game uses it as an ordinary register (~38,000 places), and Linux keeps it across context
// switches; the gates give the host its own x18 back while C runs (bionic's shadow call stack lives there).
// Host functions (HLE: NVN, rtld's callbacks, imports) keep their fake addresses at HLE_BASE, now real code: each
// 16-byte slot jumps to native_hle_gate with its index. A stub finds the thread's Cpu from TPIDR_EL0 through
// g_native_tab (native_gate.S). Everything the runtime does with a Cpu (kernel.c, services, NVN) is unchanged.
#if defined(__aarch64__) && defined(__ANDROID__)
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <pthread.h>

#include "hle.h"
#include "kernel.h"
#include "rt.h"
#include "settings.h"

typedef struct { uint64_t tp; Cpu *cpu; } NativeTabEnt;
// Open addressing from the hashed slot (native_gate.S: the same hash; up to 64 probes, past the end: no wrap). tp 0:
// never used, 1: a thread that ended (probing goes on past it). One slot per thread collided within a minute.
#define TAB_N 4096
#define TAB_PROBES 64
__attribute__((visibility("hidden"), aligned(64))) NativeTabEnt g_native_tab[TAB_N + TAB_PROBES];
static pthread_mutex_t tab_lock = PTHREAD_MUTEX_INITIALIZER;

void native_enter(Cpu *c, uint64_t addr);  // native_gate.S
extern char native_svc_gate[], native_hle_gate[];

// Chosen per boot (main.c boot). Android: always native since 2026-10-30 - the recompiled game swung its frame time
// (a walk in the prologue cave: 27.7 fps, sd 7.4 ms, against 30.0 fps, sd 0.6 ms natively) and needed hours of build;
// the app no longer ships the game code's builder. LARECOMP_NATIVE=0 (development: a totkgame.so pushed by
// android/push_game.sh) runs it recompiled. Elsewhere the setting (Native game code), LARECOMP_NATIVE=0/1 over it.
static bool g_on;
bool native_wanted(void) {
    const char *e = getenv("LARECOMP_NATIVE");
#ifdef __ANDROID__
    return e ? atoi(e) != 0 : true;
#else
    return e ? atoi(e) != 0 : g_settings.native_code;
#endif
}
void native_begin_boot(void) { g_on = native_wanted(); }
bool native_on(void) { return g_on; }

static uint64_t tp_now(void) { uint64_t v; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(v)); return v; }
static unsigned tab_idx(uint64_t tp) { return (unsigned)(((tp ^ (tp >> 12)) >> 12) & 4095); }

// The calling host thread runs guest code with this Cpu (its slot: the thread's TPIDR_EL0)
static NativeTabEnt *tab_find(uint64_t tp) {
    NativeTabEnt *e = &g_native_tab[tab_idx(tp)];
    for (int k = 0; k < TAB_PROBES && e[k].tp; k++)
        if (e[k].tp == tp) return &e[k];
    return NULL;
}
static void native_bind(Cpu *c) {
    uint64_t tp = tp_now();
    NativeTabEnt *e = tab_find(tp);
    if (e && e->cpu == c) return;
    pthread_mutex_lock(&tab_lock);
    if (!e) {
        NativeTabEnt *s = &g_native_tab[tab_idx(tp)];
        for (int k = 0; k < TAB_PROBES && !e; k++)
            if (s[k].tp <= 1) e = &s[k];  // (never used, or a thread that ended)
        if (!e) FATAL("[native] no free slot for a thread (%d probes)\n", TAB_PROBES);
    }
    e->cpu = c;
    __atomic_store_n(&e->tp, tp, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&tab_lock);
}
void native_thread_end(void) {
    if (!native_on()) return;
    pthread_mutex_lock(&tab_lock);
    NativeTabEnt *e = tab_find(tp_now());
    if (e) __atomic_store_n(&e->tp, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&tab_lock);
}

void native_call(Cpu *c, uint64_t addr) {
    native_bind(c);
    uint64_t lr = c->x[30];
    native_enter(c, addr);
    c->x[30] = lr;
}

// ---- the gates' C side (on the host stack; c holds the guest's registers)
void native_svc_c(Cpu *c, uint32_t imm) { rt_svc(c, imm, c->x[30]); }

HostFn hle_slot_fn(uint64_t pc);  // dispatch.c
void native_hle_c(Cpu *c, uint32_t i) {
    rt_unwind_point();
    HostFn f = hle_slot_fn(HLE_BASE + 16ull * i);
    f(c);
}

// ---- the rewriter
static uint32_t enc_b(uint64_t from, uint64_t to) {
    int64_t d = (int64_t)(to - from);
    if (d & 3 || d < -(1ll << 27) || d >= (1ll << 27)) FATAL("[native] branch %llx -> %llx out of range\n", from, to);
    return 0x14000000u | ((uint32_t)(d >> 2) & 0x3FFFFFFu);
}
static uint32_t enc_ldr_lit(int rt, uint64_t from, uint64_t to) { return 0x58000000u | (((uint32_t)((to - from) >> 2) & 0x7FFFF) << 5) | rt; }
static uint32_t enc_stp_pre(int t1, int t2, int n, int off) { return 0xA9800000u | (((uint32_t)(off / 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t1; }
static uint32_t enc_ldp_post(int t1, int t2, int n, int off) { return 0xA8C00000u | (((uint32_t)(off / 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t1; }
static uint32_t enc_stp(int t1, int t2, int n, int off) { return 0xA9000000u | (((uint32_t)(off / 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t1; }
static uint32_t enc_ldp(int t1, int t2, int n, int off) { return 0xA9400000u | (((uint32_t)(off / 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t1; }
static uint32_t enc_str(int t, int n, int off) { return 0xF9000000u | ((uint32_t)(off / 8) << 10) | (n << 5) | t; }
static uint32_t enc_ldr(int t, int n, int off) { return 0xF9400000u | ((uint32_t)(off / 8) << 10) | (n << 5) | t; }
#define SP 31
#define MRS_TPIDR 0xD53BD040u
#define MRS_TPIDRRO 0xD53BD060u
#define MSR_TPIDR 0xD51BD040u
#define MRS_CNTPCT 0xD53BE020u
#define MRS_CNTVCT 0xD53BE040u

// One stub per site, 80 bytes (16-aligned: the literal at +40 / +72). The svc's frame: [sp] x16, x17, [sp + 16] x30,
// [sp + 24] the svc number, [sp + 32] a third register for native_svc_gate.
static uint64_t stub_svc(uint32_t *s, uint64_t at, uint64_t site, uint32_t imm) {
    s[0] = 0xD100C3FFu;                    // sub sp, sp, #48
    s[1] = enc_stp(16, 17, SP, 0);         // stp x16, x17, [sp]
    s[2] = enc_str(30, SP, 16);            // str x30, [sp, #16]
    s[3] = 0xD2800011u | (imm << 5);       // movz x17, #imm
    s[4] = enc_ldr_lit(16, at + 16, at + 40);
    s[5] = 0xD63F0200u;                    // blr x16
    s[6] = enc_ldp(16, 17, SP, 0);         // ldp x16, x17, [sp]
    s[7] = enc_ldr(30, SP, 16);            // ldr x30, [sp, #16]
    s[8] = 0x9100C3FFu;                    // add sp, sp, #48
    s[9] = enc_b(at + 36, site + 4);
    memcpy(&s[10], &(uint64_t){(uint64_t)(uintptr_t)native_svc_gate}, 8);
    return 80;
}
// mrs xN, tpidr_el0 / tpidrro_el0 (load) or msr tpidr_el0, xN (store): xN <-> [Cpu + off], the Cpu from g_native_tab
static uint64_t stub_tls(uint32_t *s, uint64_t at, uint64_t site, int n, int off, bool store) {
    int t[3], k = 0;  // (three temporaries, none of them xN)
    for (int r = 14; r <= 17 && k < 3; r++)
        if (r != n) t[k++] = r;
    int a = t[0], b = t[1], c = t[2];
    s[0] = enc_stp_pre(a, b, SP, -32);
    s[1] = enc_str(c, SP, 16);
    s[2] = MRS_TPIDR | a;                                        // mrs xa, tpidr_el0 (the host's: the key)
    s[3] = 0xCA400000u | (a << 16) | (12 << 10) | (a << 5) | b;  // eor xb, xa, xa, lsr #12
    s[4] = 0xD3400000u | (12 << 16) | (23 << 10) | (b << 5) | b;  // ubfx xb, xb, #12, #12
    s[5] = enc_ldr_lit(c, at + 20, at + 72);                    // ldr xc, =g_native_tab
    s[6] = 0x8B000000u | (b << 16) | (4 << 10) | (c << 5) | b;   // add xb, xc, xb, lsl #4
    s[7] = enc_ldr(c, b, 0);                                    // 1: ldr xc, [xb] (the slot's thread)
    s[8] = 0xCA000000u | (a << 16) | (c << 5) | c;              // eor xc, xc, xa
    s[9] = 0xB4000000u | (3 << 5) | c;                          // cbz xc, 2f (+12)
    s[10] = 0x91004000u | (b << 5) | b;                         // add xb, xb, #16
    s[11] = 0x17FFFFFCu;                                        // b 1b (-16)
    s[12] = enc_ldr(a, b, 8);                                   // 2: ldr xa, [xb, #8] (the Cpu)
    s[13] = store ? enc_str(n, a, off) : enc_ldr(n, a, off);
    s[14] = enc_ldr(c, SP, 16);
    s[15] = enc_ldp_post(a, b, SP, 32);
    s[16] = enc_b(at + 64, site + 4);
    s[17] = 0xD503201Fu;                                        // nop (the literal 8-aligned)
    memcpy(&s[18], &(uint64_t){(uint64_t)(uintptr_t)g_native_tab}, 8);
    return 80;
}

// ---- the game's fixes (tools/patches.py, the same ones at the same addresses: there they are C in place of one
// instruction of the generated code). Here a stub calls native_patch_gate with the fix's number; its handler works on
// the Cpu and says how to go on: 0 run the original instruction (from the stub) and go on after it, 1 the fix's
// other address, 2 return from the function (ret), 3 go on after the instruction without running it.
extern char native_patch_gate[];
// TOTK 1.4.3's fixes (tools/patches.py, there each the C in place of the instruction; the comments there say why)
uint32_t rt_dynres_scale(uint32_t game);    // nvn/objects.c
uint32_t rt_dynres_tier(uint64_t ctl, uint32_t cur, uint32_t want);
float rt_frame_ratio30(float nominal);
float rt_frame_interval60(float nominal);
float rt_cloth_step(float nominal_s);
float rt_cloth_ratio(void);
float rt_render_size(int axis, int32_t game, int32_t output_w);
static float vget(const Cpu *c, int r) { float f; memcpy(&f, &c->v[r], 4); return f; }
static void vset(Cpu *c, int r, float f) { c->v[r] = 0; memcpy(&c->v[r], &f, 4); }
static int fix_dynres_scale(Cpu *c) { c->x[9] = rt_dynres_scale((uint32_t)c->x[23]); return 3; }  // mov w9, w23
static int dynres_tier(Cpu *c, int reg) {  // str w<reg>, [x19, #0x134]
    uint32_t *p = (uint32_t *)(uintptr_t)(c->x[19] + 0x134);
    *p = rt_dynres_tier(c->x[19], *p, (uint32_t)c->x[reg]);
    return 3;
}
static int fix_dynres_tier24(Cpu *c) { return dynres_tier(c, 24); }
static int fix_dynres_tier0(Cpu *c) { return dynres_tier(c, 0); }
static int fix_step_ratio(Cpu *c) {  // ldr s0, [x19, #0x250] -> the step ratio itself
    int32_t fps = *(const int32_t *)(uintptr_t)(c->x[19] + 0x250);
    vset(c, 0, rt_frame_ratio30(30.0f / (float)fps));
    return 3;
}
static int fix_drop(Cpu *c) { (void)c; return 3; }  // (the ratio is already in s0)
static int fix_step_interval(Cpu *c) {  // fmul s8, s0, s1 (s0: 60 / fps)
    float s0 = rt_frame_interval60(vget(c, 0));
    vset(c, 8, s0 * vget(c, 1));
    vset(c, 0, s0);
    return 3;
}
static int cloth_inline(Cpu *c, uint32_t ratio_off, int factor, int nominal, int dest) {
    float fac = vget(c, factor), step = rt_cloth_step(vget(c, nominal)), ratio = step * 30.0f * fac;
    memcpy((void *)(uintptr_t)(c->sp + ratio_off), &ratio, 4);
    vset(c, dest, step * fac);
    return 3;
}
static int fix_cloth_246eac(Cpu *c) { return cloth_inline(c, 0x364, 10, 0, 0); }  // fmul s0, s10, s0
static int fix_cloth_345904(Cpu *c) { return cloth_inline(c, 0x114, 0, 1, 1); }   // fmul s1, s0, s1
static int cloth_call_args(Cpu *c, int src) {  // mov w6, w<src>, and the clock in s2 / s3 for the bl 0x2a0e40 next
    c->x[6] = (uint32_t)c->x[src];
    float r = rt_cloth_ratio();
    vset(c, 2, vget(c, 2) * r);
    vset(c, 3, vget(c, 3) * r);
    return 3;
}
static int fix_cloth_249448(Cpu *c) { return cloth_call_args(c, 20); }
static int fix_cloth_28bc94(Cpu *c) { return cloth_call_args(c, 26); }
static int fix_render_w(Cpu *c) { vset(c, 0, rt_render_size(0, (int32_t)c->x[11], (int32_t)c->x[9])); return 3; }  // scvtf s0, w11
static int fix_render_h(Cpu *c) { vset(c, 0, rt_render_size(1, (int32_t)c->x[10], (int32_t)c->x[9])); return 3; }  // scvtf s0, w10
// 60 fps: the frame controller's lowest present interval 1 instead of [x20] (patches.py's seven sites). Here w8 = 1
// before its first cmp does what their cmp / csel with #1 do (w8 stays the lowest until 0x2db238's mov or 0x2db340's
// reload), so four of them are enough: no flags to compute.
bool rt_fps60(void);  // nvn/objects.c
static int fix_fps60_cmp(Cpu *c) { if (rt_fps60()) c->x[8] = 1; return 0; }                // cmp w10, w8
static int fix_fps60_skip(Cpu *c) { return rt_fps60() ? 3 : 0; }                          // ldr w10, [x20, #0x44]
static int fix_fps60_lowest(Cpu *c) { if (!rt_fps60()) return 0; c->x[8] = 1; return 3; }  // ldr w8, [x20]
static int fix_fps60_pick(Cpu *c) {  // mov w21, w10 (after cmp w10, w8 / b.gt: the interval, at least 1)
    if (!rt_fps60()) return 0;
    c->x[21] = (int32_t)c->x[10] >= 1 ? (uint32_t)c->x[10] : 1;
    return 3;
}
static int fade_step(Cpu *c, int rd) {  // fcvtzs w<rd>, s0: a fade's step, at least 1 when positive
    float s0 = vget(c, 0);
    int32_t r = s0 != s0 ? 0 : s0 >= 2147483648.0f ? 0x7fffffff : s0 <= -2147483648.0f ? (int32_t)0x80000000u : (int32_t)s0;
    if (r == 0 && s0 > 0) r = 1;
    c->x[rd] = (uint32_t)r;
    return 3;
}
void rt_cutscene_tick(int which);  // nvn/objects.c: a cutscene function's entry (patches.py's four)
static int fix_cutscene0(Cpu *c) { (void)c; rt_cutscene_tick(0); return 0; }
static int fix_cutscene1(Cpu *c) { (void)c; rt_cutscene_tick(1); return 0; }
static int fix_cutscene2(Cpu *c) { (void)c; rt_cutscene_tick(2); return 0; }
static int fix_cutscene3(Cpu *c) { (void)c; rt_cutscene_tick(3); return 0; }
uint32_t rt_interval_pick(uint32_t game);
static int fix_interval_pick(Cpu *c) { c->x[21] = rt_interval_pick((uint32_t)c->x[21]); return 0; }  // mov w8, #0x3c
// Free aspect: the four places 1.4.3 hands an aspect ratio to a camera's perspective projection (sead's +0xb0), the
// sites MacNativeGames found for its own wide screens: the camera's update and setup copying it, and two viewport
// width / height divisions. Each runs here and gives a ratio near 16:9 the window's shape (hle/widescreen.c).
float rt_widen_aspect(float a);
static int fix_aspect_load(Cpu *c, int rn, uint32_t off) {  // ldr s0, [x<rn>, #off]
    float a;
    memcpy(&a, (const void *)(uintptr_t)(c->x[rn] + off), 4);
    vset(c, 0, rt_widen_aspect(a));
    return 3;
}
static int fix_aspect_cam(Cpu *c) { return fix_aspect_load(c, 19, 0x140); }
static int fix_aspect_setup(Cpu *c) { return fix_aspect_load(c, 22, 0x68); }
static int fix_aspect_view(Cpu *c) { vset(c, 0, rt_widen_aspect(vget(c, 0) / vget(c, 1))); return 3; }  // fdiv s0, s0, s1
// Equipment-switch cooldown: the threshold the player update steps its countdown down to, FLT_EPSILON in the game, 0 on
// real time - a measured step's remainder in (0, FLT_EPSILON] kept weapon, shield and bow changes locked until a load.
bool rt_realtime(void);  // nvn/objects.c
static int fix_cooldown(Cpu *c) { if (!rt_realtime()) return 0; c->v[1] = 0; return 3; }  // movi v1.2s, #0x34, lsl #24
static int fix_fade_w8(Cpu *c) { return fade_step(c, 8); }
static int fix_fade_w10(Cpu *c) { return fade_step(c, 10); }
typedef struct { int version; uint64_t addr, alt; uint32_t word; int (*fn)(Cpu *); const char *what; } Fix;
static const Fix fixes[] = {  // (canonical addresses, 0x7000000000 + the offset in main)
    {143, 0x70005848a8, 0, 0x2a1703e9, fix_dynres_scale, "dynamic resolution scale"},
    {143, 0x7000584a70, 0, 0xb9013678, fix_dynres_tier24, "dynamic resolution tier (up)"},
    {143, 0x7000584b70, 0, 0xb9013660, fix_dynres_tier0, "dynamic resolution tier (reset)"},
    {143, 0x7000584b94, 0, 0xb9013678, fix_dynres_tier24, "dynamic resolution tier (down)"},
    {143, 0x70002daa54, 0, 0xbd425260, fix_step_ratio, "world step ratio"},
    {143, 0x70002daa6c, 0, 0x5e21d800, fix_drop, "world step (scvtf)"},
    {143, 0x70002daa70, 0, 0x1e201820, fix_drop, "world step (fdiv)"},
    {143, 0x70002daa78, 0, 0x1e210800, fix_drop, "world step (fmul)"},
    {143, 0x70003e5658, 0, 0x1e210808, fix_step_interval, "world step interval"},
    {143, 0x7000246eac, 0, 0x1e200940, fix_cloth_246eac, "cloth clock (inline)"},
    {143, 0x7000345904, 0, 0x1e210801, fix_cloth_345904, "cloth clock (inline)"},
    {143, 0x7000249448, 0, 0x2a1403e6, fix_cloth_249448, "cloth clock (call)"},
    {143, 0x700028bc94, 0, 0x2a1a03e6, fix_cloth_28bc94, "cloth clock (call)"},
    {143, 0x7000585e48, 0, 0x1e220160, fix_render_w, "scene width"},
    {143, 0x7000585e54, 0, 0x1e220140, fix_render_h, "scene height"},
    {143, 0x70002db204, 0, 0x6b08015f, fix_fps60_cmp, "60 fps (the lowest interval)"},
    {143, 0x70002db22c, 0, 0xb940468a, fix_fps60_skip, "60 fps (the interval tried)"},
    {143, 0x70002db340, 0, 0xb9400288, fix_fps60_lowest, "60 fps (the lowest again)"},
    {143, 0x70002db34c, 0, 0x2a0a03f5, fix_fps60_pick, "60 fps (the interval picked)"},
    {143, 0x70000a7ac4, 0, 0x1e380008, fix_fade_w8, "fade step"},
    {143, 0x700095dd24, 0, 0x1e380008, fix_fade_w8, "fade step"},
    {143, 0x70006155e4, 0, 0x1e38000a, fix_fade_w10, "fade step"},
    {143, 0x7000615660, 0, 0x1e38000a, fix_fade_w10, "fade step"},
    {143, 0x700045c700, 0, 0xd10103ff, fix_cutscene0, "cutscene mark"},
    {143, 0x7002b23c7c, 0, 0xd100c3ff, fix_cutscene1, "cutscene mark"},
    {143, 0x700042c3bc, 0, 0xd10283ff, fix_cutscene2, "cutscene mark"},
    {143, 0x7000a5a188, 0, 0xf9401008, fix_cutscene3, "cutscene mark"},
    {143, 0x70002db238, 0, 0x52800788, fix_interval_pick, "the interval picked (2 in cutscenes)"},
    {143, 0x7000628c74, 0, 0xbd414260, fix_aspect_cam, "camera aspect (update)"},
    {143, 0x7000b43460, 0, 0xbd406ac0, fix_aspect_setup, "camera aspect (setup)"},
    {143, 0x7001ff04f4, 0, 0x1e211800, fix_aspect_view, "camera aspect (viewport)"},
    {143, 0x700253fbb0, 0, 0x1e211800, fix_aspect_view, "camera aspect (viewport)"},
    {143, 0x70002b036c, 0, 0x0f016681, fix_cooldown, "equipment-switch cooldown"},
};
int native_patch_c(Cpu *c, uint32_t i) { return fixes[i].fn(c); }

static uint64_t stub_fix(uint32_t *s, uint64_t at, uint64_t site, uint32_t id, uint32_t orig, uint64_t alt) {
    const uint32_t pop[3] = {enc_ldp(16, 17, SP, 0), enc_ldr(30, SP, 16), 0x9100C3FFu};  // (the frame back, add sp #48)
    s[0] = 0xD100C3FFu;                         // sub sp, sp, #48
    s[1] = enc_stp(16, 17, SP, 0);
    s[2] = enc_str(30, SP, 16);
    s[3] = 0xD2800011u | (id << 5);             // movz x17, #id
    s[4] = enc_ldr_lit(16, at + 16, at + 112);
    s[5] = 0xD63F0200u;                         // blr x16 (native_patch_gate: the way on at [sp + 24])
    s[6] = enc_ldr(16, SP, 24);
    s[7] = 0xB4000000u | (15 << 5) | 16;        // cbz x16, 22 (+60)
    s[8] = 0x37080000u | (5 << 5) | 16;         // tbnz w16, #1, 13 (+20)
    memcpy(&s[9], pop, 12);                     // 1: the fix's other address
    s[12] = alt ? enc_b(at + 48, alt) : 0xD4200000u;
    s[13] = 0x37000000u | (5 << 5) | 16;        // tbnz w16, #0, 18 (+20)
    memcpy(&s[14], pop, 12);                    // 2: return
    s[17] = 0xD65F03C0u;                        // ret
    memcpy(&s[18], pop, 12);                    // 3: on, without the instruction
    s[21] = enc_b(at + 84, site + 4);
    memcpy(&s[22], pop, 12);                    // 0: the instruction, then on
    s[25] = orig;
    s[26] = enc_b(at + 104, site + 4);
    s[27] = 0xD503201Fu;                        // nop (the literal 8-aligned)
    memcpy(&s[28], &(uint64_t){(uint64_t)(uintptr_t)native_patch_gate}, 8);
    return 128;
}

static void prepare_module(Module *m) {
    uint32_t *text = (uint32_t *)(uintptr_t)(m->base + m->text_off);
    uint64_t nw = m->text_size / 4, nsites = 0;
    for (uint64_t i = 0; i < nw; i++) {
        uint32_t w = text[i];
        if ((w & 0xFFE0001Fu) == 0xD4000001u || (w & ~31u) == MRS_TPIDR || (w & ~31u) == MRS_TPIDRRO || (w & ~31u) == MSR_TPIDR)
            nsites++;
    }
    bool is_main = m->base == MAIN_BASE;
    uint64_t nfix = sizeof fixes / sizeof *fixes;
    uint64_t area = (m->base + m->size + 0xFFFF) & ~0xFFFFull, asize = (nsites * 80 + nfix * 128 + 0x3FFF) & ~0x3FFFull;
    if (!asize) asize = 0x4000;
    plat_commit(area, asize);
    uint64_t at = area;
    unsigned nsvc = 0, ntls = 0, ncnt = 0;
    for (uint64_t i = 0; i < nw; i++) {
        uint32_t w = text[i];
        uint64_t site = (uint64_t)(uintptr_t)&text[i];
        uint64_t len = 0;
        if ((w & 0xFFE0001Fu) == 0xD4000001u) len = stub_svc((uint32_t *)(uintptr_t)at, at, site, (w >> 5) & 0xFFFF), nsvc++;
        else if ((w & ~31u) == MRS_TPIDRRO) len = stub_tls((uint32_t *)(uintptr_t)at, at, site, w & 31, 288, false), ntls++;
        else if ((w & ~31u) == MRS_TPIDR) len = stub_tls((uint32_t *)(uintptr_t)at, at, site, w & 31, 280, false), ntls++;
        else if ((w & ~31u) == MSR_TPIDR) len = stub_tls((uint32_t *)(uintptr_t)at, at, site, w & 31, 280, true), ntls++;
        else if ((w & ~31u) == MRS_CNTPCT) { text[i] = MRS_CNTVCT | (w & 31); ncnt++; continue; }
        if (!len) continue;
        text[i] = enc_b(site, at);
        at += len;
    }
    unsigned nfixed = 0;
    for (uint32_t f = 0; is_main && f < nfix; f++) {
        if (fixes[f].version != g_game_version) continue;
        uint64_t site = MAIN_BASE + (fixes[f].addr - 0x7000000000ull);
        uint64_t alt = fixes[f].alt ? MAIN_BASE + (fixes[f].alt - 0x7000000000ull) : 0;
        uint32_t *w = (uint32_t *)(uintptr_t)site;
        if (*w != fixes[f].word) FATAL("[native] fix '%s' at %llx: %08x, not %08x\n", fixes[f].what,
                                       (unsigned long long)fixes[f].addr, *w, fixes[f].word);
        at += stub_fix((uint32_t *)(uintptr_t)at, at, site, f, *w, alt);
        *w = enc_b(site, at - 128);
        nfixed++;
    }
    uint64_t t0 = m->base + m->text_off, tsz = (m->text_size + 0xFFF) & ~0xFFFull;
    __builtin___clear_cache((char *)(uintptr_t)t0, (char *)(uintptr_t)(t0 + tsz));
    __builtin___clear_cache((char *)(uintptr_t)area, (char *)(uintptr_t)(area + asize));
    if (mprotect((void *)(uintptr_t)t0, tsz, PROT_READ | PROT_EXEC) || mprotect((void *)(uintptr_t)area, asize, PROT_READ | PROT_EXEC))
        FATAL("[native] %s: the code can't be made executable (errno %d)\n", m->name, errno);
    LOG("[native] %s: %u svc, %u TLS, %u counter reads rewritten, %u fixes; stubs at %llx (%llu KB)\n", m->name, nsvc,
        ntls, ncnt, nfixed, (unsigned long long)area, (unsigned long long)(asize >> 10));
}

// The HLE slots as code: slot i = movz x17, #i; b trampoline (-> native_hle_gate)
static void prepare_hle(void) {
    uint64_t n = 4096, tramp = HLE_BASE + 16 * n, size = 16 * n + 0x4000;
    plat_commit(HLE_BASE, size);
    for (uint64_t i = 0; i < n; i++) {
        uint32_t *s = (uint32_t *)(uintptr_t)(HLE_BASE + 16 * i);
        s[0] = 0xD2800011u | ((uint32_t)i << 5);
        s[1] = enc_b(HLE_BASE + 16 * i + 4, tramp);
        s[2] = s[3] = 0xD4200000u;  // brk #0
    }
    uint32_t *t = (uint32_t *)(uintptr_t)tramp;
    t[0] = enc_ldr_lit(16, tramp, tramp + 8);
    t[1] = 0xD61F0200u;  // br x16
    memcpy(&t[2], &(uint64_t){(uint64_t)(uintptr_t)native_hle_gate}, 8);
    __builtin___clear_cache((char *)(uintptr_t)HLE_BASE, (char *)(uintptr_t)(HLE_BASE + size));
    if (mprotect((void *)(uintptr_t)HLE_BASE, size, PROT_READ | PROT_EXEC))
        FATAL("[native] the HLE slots can't be made executable (errno %d)\n", errno);
}

void native_prepare(void) {
    if (!native_on()) return;
    uint64_t f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    LOG("[native] the game's code runs on the CPU (counter %llu Hz%s)\n", (unsigned long long)f,
        f == 19200000 ? ", as the Switch's" : ": NOT the Switch's 19.2 MHz - the game's timing will be off");
    memset(g_native_tab, 0, sizeof g_native_tab);
    for (int i = 0; i < g_nmodules; i++) prepare_module(&g_modules[i]);
    prepare_hle();
}

// The Switch's tick: the counter the rewritten reads see (rt_ticks, kernel.c)
uint64_t native_ticks(void) { uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }
#else
#include "rt.h"
bool native_wanted(void) { return false; }
void native_begin_boot(void) {}
bool native_on(void) { return false; }
void native_prepare(void) {}
void native_call(Cpu *c, uint64_t addr) { (void)c; (void)addr; }
void native_thread_end(void) {}
uint64_t native_ticks(void) { return 0; }
#endif
