"""Per-instruction replacements applied by gen.py (guest address -> function(gen, addr) returning C code).

Each entry replaces the one instruction at that address of `main` (canonical address, 0x7000000000 + offset) with the C
the function returns; gen.py rebases the addresses in it for --dyn-base. Check the instruction word first (an assert),
so a wrong build fails here instead of patching the wrong code. Example, from LAS Recomp's widescreen: a camera's
`ldr sN, [xM, #imm]` of the 16:9 aspect literal, replaced by a call into the runtime:

    def _aspect_load(gen, a):
        w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
        assert (w & 0xffc00000) == 0xbd400000, hex(w)  # ldr s, [x, #imm]
        rt = w & 31
        return (f'{{ extern float rt_aspect_site(int); float f = rt_aspect_site(0); '
                f'c->v[{rt}] = 0; memcpy(&c->v[{rt}], &f, 4); }}  /* {a:#x}: ldr s{rt}, =16/9 */')

BOTWRecompAndroid's patches (its frame pacing, widescreen and a reload crash, at BOTW's addresses) are in this file's
git history and docs/botw/BOTW-PORT.md.

Tears of the Kingdom 1.4.3's sites come from MacNativeGames (github.com/ly05010419/MacNativeGames,
games/totk/runtime/game_patches_v143.c), which runs the same build on Macs and checks each original instruction the
same way: its addresses and words matched this build's main one for one.
"""
import struct

BUILD_TOTK_1_4_3 = '277178b7dba1b6d4'
PATCHES_143 = {}  # Tears of the Kingdom 1.4.3 (main build 277178b7...; 1.4.2, the port's first, had none)


def _word(gen, a):
    return struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]


def _expect(gen, a, word):
    w = _word(gen, a)
    assert w == word, f'{a:#x}: {w:08x}, expected {word:08x} (not TOTK 1.4.3?)'


# The dynamic resolution controller (main+0x584300, from the renderer's per-frame update) steps the scene through
# 100 / 90 / 80 % tiers (1600x896, 1432x808, 1280x720) from its frame-time prediction and returns the tier's scale in
# x0's low word; every path leaves through this one move. Each step gave a frame whose world wasn't drawn (black but
# for the particles and the HUD): runtime rt_dynres_scale holds it at 100 % unless LARECOMP_DYNRES=1.
def _dynres_scale(gen, a):
    _expect(gen, a, 0x2a1703e9)  # mov w9, w23
    return ('{ extern uint32_t rt_dynres_scale(uint32_t); c->x[9] = (uint64_t)rt_dynres_scale((uint32_t)c->x[23]); }'
            f'  /* {a:#x}: mov w9, w23 (the tier\'s scale) */')


# The controller's tier (an id into its list of tiers, -1 none) is stored at [x19 + 0x134] by its three paths: a step
# down, a step up, a reset. With only the scale held (above) the tier kept stepping - 40 steps in 3 min of walking the
# cave, 2026-10-26 - and with it the flag the caller gets in bit 33 of the return (the tier the same as at the last
# call), each step once a frame without the scenery. rt_dynres_tier keeps the stored tier unless the new one scales
# higher (it stays at -1, 100 %, all session; LARECOMP_DYNRES=1: every step). (The scenery missing in cutscenes is not
# this: it stays with the tier held - docs/TOTK-PORT.md.)
def _dynres_tier_store(reg, word):
    def patch(gen, a):
        _expect(gen, a, word)
        return ('{ extern uint32_t rt_dynres_tier(uint64_t, uint32_t, uint32_t); uint64_t p = c->x[19] + 0x134ull; '
                f'ST32(p, rt_dynres_tier(c->x[19], LD32(p), (uint32_t)c->x[{reg}])); }}'
                f'  /* {a:#x}: str w{reg}, [x19, #0x134] (the tier) */')
    return patch


# The world's frame step: FrameRateMgr derives it from the nominal frame rate ([x19+0x250], 30), as 60/fps x 0.5 (the
# step relative to 30 fps, stored for the frame) and 60/fps x the half-step selector (the interval in 60 Hz vsyncs).
# A phone that renders 22 fps then played in slow motion. runtime rt_frame_ratio30 / rt_frame_interval60 put the
# measured time between presents in their place (LARECOMP_REALTIME=0: the nominal values, as the original computes).
def _step_ratio_load(gen, a):
    _expect(gen, a, 0xbd425260)  # ldr s0, [x19, #0x250]
    return ('{ extern float rt_frame_ratio30(float); int32_t fps = (int32_t)LD32(c->x[19] + 0x250ull); '
            'float f = rt_frame_ratio30(30.0f / (float)fps); c->v[0] = 0; memcpy(&c->v[0], &f, 4); }'
            f'  /* {a:#x}: ldr s0, [x19, #0x250] -> the step ratio itself */')


def _drop(word, what):
    def patch(gen, a):
        _expect(gen, a, word)
        return f'/* {a:#x}: {what} (tools/patches.py: the ratio is already in s0) */'
    return patch


def _step_interval(gen, a):
    _expect(gen, a, 0x1e210808)  # fmul s8, s0, s1
    return ('{ extern float rt_frame_interval60(float); float s0, s1; memcpy(&s0, &c->v[0], 4); memcpy(&s1, &c->v[1], 4); '
            's0 = rt_frame_interval60(s0); float s8 = s0 * s1; c->v[0] = 0; memcpy(&c->v[0], &s0, 4); '
            'c->v[8] = 0; memcpy(&c->v[8], &s8, 4); }'
            f'  /* {a:#x}: fmul s8, s0, s1 (s0: 60 / fps) */')


# Cloth and hair: the actor animation update has a second, unscaled clock (ordinary animation gets FrameRateMgr's step,
# above). Two actor paths build it inline - {seconds = factor x 1/30, ratio = factor} on the stack - and two others pass
# it in s2 / s3 to the shared update at main+0x2a0e40 (its third caller already passes measured time: left alone).
# With the world on real time and the phone off 30 fps, cloth and hair ran at the wrong speed. rt_cloth_step /
# rt_cloth_ratio: the measured step (LARECOMP_REALTIME=0 or LARECOMP_CLOTH_REALTIME=0: the nominal, bit for bit).
def _cloth_inline(seconds_off, ratio_off, factor, nominal, dest, word, what):
    def patch(gen, a):
        _expect(gen, a, word)
        return ('{ extern float rt_cloth_step(float); float fac, nom; '
                f'memcpy(&fac, &c->v[{factor}], 4); memcpy(&nom, &c->v[{nominal}], 4); float step = rt_cloth_step(nom); '
                f'float ratio = step * 30.0f * fac, sec = step * fac; uint32_t rb; memcpy(&rb, &ratio, 4); '
                f'ST32(c->sp + {ratio_off:#x}ull, rb); c->v[{dest}] = 0; memcpy(&c->v[{dest}], &sec, 4); }}'
                f'  /* {a:#x}: {what} (seconds -> sp+{seconds_off:#x} next, ratio at sp+{ratio_off:#x}) */')
    return patch


def _cloth_call_args(word, src, what):
    def patch(gen, a):
        _expect(gen, a, word)
        return (f'{{ c->x[6] = (uint64_t)(uint32_t)c->x[{src}]; extern float rt_cloth_ratio(void); float r = rt_cloth_ratio(), '
                's2, s3; memcpy(&s2, &c->v[2], 4); memcpy(&s3, &c->v[3], 4); s2 *= r; s3 *= r; '
                'c->v[2] = 0; memcpy(&c->v[2], &s2, 4); c->v[3] = 0; memcpy(&c->v[3], &s3, 4); }'
                f'  /* {a:#x}: {what}, and the clock in s2 / s3 for the bl 0x2a0e40 next */')
    return patch


# 60 fps: the frame controller (main+0x2db180, from the frame loop; it calls nvnWindowSetPresentInterval from
# 0x2db2e8) picks the present interval from the measured frame work, never under its lowest ([x20], 2: 30 fps).
# MacNativeGames' seven instructions take that lowest as 1 instead, so it picks 60 where a frame fits and 30 where it
# doesn't. Each site here is both instructions, the original and theirs (the lifter's own C for each), chosen at run
# time: rt_fps60() is Video > Frame rate Auto or 60 (LARECOMP_FPS60=0/1 overrides).
def _fps60(word, new):
    def patch(gen, a):
        _expect(gen, a, word)
        old_c, new_c = (gen.lifter.lift(w, a) for w in (word, new))
        assert old_c.kind == new_c.kind == 'op', (old_c.kind, new_c.kind)
        return (f'{{ extern _Bool rt_fps60(void); if (rt_fps60()) {{ {new_c.code} }} else {{ {old_c.code} }} }}'
                f'  /* {a:#x}: {word:08x} or, at 60 fps, {new:08x} (the lowest interval 1) */')
    return patch


for _a, _old, _new in ((0x70002db204, 0x6b08015f, 0x7100055f),   # cmp w10, w8          -> cmp w10, #1
                       (0x70002db208, 0x1a89b10a, 0x1a9fa52a),   # csel w10, w8, w9, lt -> csinc w10, w9, wzr, ge
                       (0x70002db22c, 0xb940468a, 0xd503201f),   # ldr w10, [x20, #0x44] -> nop
                       (0x70002db340, 0xb9400288, 0x52800028),   # ldr w8, [x20]        -> mov w8, #1
                       (0x70002db34c, 0x2a0a03f5, 0x1a9fa555),   # mov w21, w10         -> csinc w21, w10, wzr, ge
                       (0x70002db360, 0x6b0801bf, 0x710005bf),   # cmp w13, w8          -> cmp w13, #1
                       (0x70002db36c, 0x1a88c1ac, 0x1a9fc5ac)):  # csel w12, w13, w8, gt -> csinc w12, w13, wzr, gt
    PATCHES_143[_a] = _fps60(_old, _new)


# Fades (objects fading in and out: four places step a byte counter by trunc(2.1 x the step ratio), the ratio 1 at 30
# fps): at 60 the ratio is ~0.5 and a frame a little shorter than 1/60 s steps 0 - MacNativeGames saw objects stay
# invisible or half transparent above ~63.5 fps. A positive step is at least 1 (a paused game's 0 stays 0).
def _fade_step(rd):
    def patch(gen, a):
        _expect(gen, a, 0x1e380000 | rd)  # fcvtzs w<rd>, s0 (saturating, NaN 0)
        return ('{ float s0; memcpy(&s0, &c->v[0], 4); int32_t r = s0 != s0 ? 0 : s0 >= 2147483648.0f ? 0x7fffffff : '
                's0 <= -2147483648.0f ? (int32_t)0x80000000u : (int32_t)s0; if (r == 0 && s0 > 0) r = 1; '
                f'c->x[{rd}] = (uint32_t)r; }}  /* {a:#x}: fcvtzs w{rd}, s0 (a fade\'s step, at least 1) */')
    return patch


# Cutscenes: some of their timing counts frames - above 30 fps they played fast. Four functions that run once a frame
# while one plays and never in the title or in play (counted over the new game's opening, LARECOMP_FNPROF, 2026-10-29;
# from three different call chains, so one tied to that scene alone still leaves the others): each entry marks the
# frame, and the presents wait for 30 fps while one has run in the last 200 ms (nvn/objects.c rt_cutscene_tick).
def _cutscene_mark(word, which):
    def patch(gen, a):
        _expect(gen, a, word)
        ins = gen.lifter.lift(word, a)
        assert ins.kind == 'op', ins.kind
        return (f'{{ extern void rt_cutscene_tick(int); rt_cutscene_tick({which}); {ins.code} }}'
                f'  /* {a:#x}: a cutscene function\'s entry ({which}) */')
    return patch


for _i, (_a, _w) in enumerate(((0x700045c700, 0xd10103ff), (0x7002b23c7c, 0xd100c3ff),
                               (0x700042c3bc, 0xd10283ff), (0x7000a5a188, 0xf9401008))):
    PATCHES_143[_a] = _cutscene_mark(_w, _i)


# ... and the frame controller picks 2 while one plays: every path that sets its interval (w21) meets here, before it is
# stored ([x20+0x40], [x19+0x88]) and given to nvnWindowSetPresentInterval. Some cutscene animation steps by that
# interval, not the measured time: the game must believe the 30 fps it is held at (rt_interval_pick).
def _interval_pick(gen, a):
    _expect(gen, a, 0x52800788)  # mov w8, #0x3c
    return ('{ extern uint32_t rt_interval_pick(uint32_t); c->x[21] = rt_interval_pick((uint32_t)c->x[21]); '
            f'c->x[8] = 0x3c; }}  /* {a:#x}: mov w8, #0x3c (w21: the interval picked) */')


PATCHES_143[0x70002db238] = _interval_pick

PATCHES_143[0x70000a7ac4] = _fade_step(8)
PATCHES_143[0x700095dd24] = _fade_step(8)
PATCHES_143[0x70006155e4] = _fade_step(10)
PATCHES_143[0x7000615660] = _fade_step(10)

# Equipment-switch cooldown (site from totk-pc-recomp recomp/patches-v143.txt, MIT; their issue #18, found by JieML1100;
# seen in UltraTears): the player update (main+0x2b0368) steps the countdown at player state +0x109c only while it is
# above FLT_EPSILON, but the equipment gate (main+0x31cc7c) blocks weapon, shield and bow changes while it is above 0.
# Whole 30 Hz steps reach exactly 0; a measured step can leave a remainder in (0, FLT_EPSILON] that is never stepped
# again, and equipment stayed locked until a load. On real time (rt_realtime) the threshold is 0: the remainder goes
# below 0 like any last step. LARECOMP_REALTIME=0: the original's FLT_EPSILON.
def _cooldown(gen, a):
    _expect(gen, a, 0x0f016681)  # movi v1.2s, #0x34, lsl #24 (FLT_EPSILON in both lanes)
    return ('{ extern _Bool rt_realtime(void); c->v[1] = rt_realtime() ? 0 : (__uint128_t)0x3400000034000000ull; }'
            f'  /* {a:#x}: movi v1.2s, #0x34, lsl #24, or #0 on real time (the cooldown\'s threshold) */')


PATCHES_143[0x70002b036c] = _cooldown

PATCHES_143[0x7000246eac] = _cloth_inline(0x360, 0x364, 10, 0, 0, 0x1e200940, 'fmul s0, s10, s0')
PATCHES_143[0x7000345904] = _cloth_inline(0x110, 0x114, 0, 1, 1, 0x1e210801, 'fmul s1, s0, s1')
PATCHES_143[0x7000249448] = _cloth_call_args(0x2a1403e6, 20, 'mov w6, w20')
PATCHES_143[0x700028bc94] = _cloth_call_args(0x2a1a03e6, 26, 'mov w6, w26')

# The scene's render size: every path that picks it (the mode entry's: handheld 1280x720, docked 1600x900) converts its
# width and height to float here, into the renderer's +0xcc4 / +0xcc8 (w9: the output width, +0xcc0; the game scales the
# scene by output width / 1920 past 1920). rt_render_size: LARECOMP_RENDER_RES=<w>x<h> (MacNativeGames' iPhone default
# is 960x540, the game's FSR 1 upscaling it: GPU 24.3 -> 17.3 ms there), else the game's own.
def _render_size(axis, reg, word):
    def patch(gen, a):
        _expect(gen, a, word)
        return (f'{{ extern float rt_render_size(int, int32_t, int32_t); float f = rt_render_size({axis}, '
                f'(int32_t)c->x[{reg}], (int32_t)c->x[9]); c->v[0] = 0; memcpy(&c->v[0], &f, 4); }}'
                f'  /* {a:#x}: scvtf s0, w{reg} (the scene\'s {"width" if axis == 0 else "height"}) */')
    return patch


PATCHES_143[0x7000585e48] = _render_size(0, 11, 0x1e220160)
PATCHES_143[0x7000585e54] = _render_size(1, 10, 0x1e220140)

PATCHES_143[0x70005848a8] = _dynres_scale
PATCHES_143[0x7000584a70] = _dynres_tier_store(24, 0xb9013678)
PATCHES_143[0x7000584b70] = _dynres_tier_store(0, 0xb9013660)
PATCHES_143[0x7000584b94] = _dynres_tier_store(24, 0xb9013678)
PATCHES_143[0x70002daa54] = _step_ratio_load
PATCHES_143[0x70002daa6c] = _drop(0x5e21d800, 'scvtf s0, s0')
PATCHES_143[0x70002daa70] = _drop(0x1e201820, 'fdiv s0, s1, s0')
PATCHES_143[0x70002daa78] = _drop(0x1e210800, 'fmul s0, s0, s1')
PATCHES_143[0x70003e5658] = _step_interval


def patches_for(build_id):
    """The patches for a module, by its build id (sdk and anything unknown: none)."""
    return {BUILD_TOTK_1_4_3: PATCHES_143}.get(build_id[:16], {})
