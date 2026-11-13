// Instruction-semantics regression test for the Maxwell shader translator (runtime/nvn/maxwell/translate.c).
//
// Each case is a tiny compute program: MOV32I loads the inputs, then the instruction under test. Its MSL body
// (the GLSL is the same text after glsl_rewrite) is written into a C++ file with stand-ins for the few MSL
// built-ins it uses, compiled and run on the CPU, and the registers / predicates are compared with values worked
// out by hand from the instruction's definition. tools/test_translate_ops.sh drives it.
//
// usage: test_translate_ops <out.cpp>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/nvn/maxwell/shader.h"

#define PT_GUARD (7ull << 16)  // @PT
#define NOP 0x50b0000000070f00ull
#define EXIT 0xe30000000007000full

static uint64_t mov32i(int d, uint32_t v) { return 0x0100000000000000ull | (uint64_t)v << 20 | 0xFull << 12 | PT_GUARD | (uint64_t)d; }

// I2I Rd, Rb (register form). Sizes: 0 8-bit, 1 16-bit, 2 32-bit.
static uint64_t i2i(int d, int b, int dsz, bool dsg, int ssz, bool ssg, bool sat, bool abs_, bool neg) {
    return 0x5ce0000000000000ull | PT_GUARD | (uint64_t)d | (uint64_t)dsz << 8 | (uint64_t)ssz << 10 | (uint64_t)dsg << 12 |
           (uint64_t)ssg << 13 | (uint64_t)b << 20 | (uint64_t)neg << 45 | (uint64_t)abs_ << 49 | (uint64_t)sat << 50;
}

// SHF.{L,R} Rd, Ra (low), shift, Rc (high). width: 0 32-bit, 2 .U64, 3 .S64. shift_reg < 0: immediate `imm`.
static uint64_t shf(bool right, int d, int a, int shift_reg, uint32_t imm, int c, int width, bool wrap) {
    uint64_t w = shift_reg >= 0 ? (right ? 0x5cf8000000000000ull : 0x5bf8000000000000ull) | (uint64_t)shift_reg << 20
                                : (right ? 0x38f8000000000000ull : 0x36f8000000000000ull) | (uint64_t)(imm & 63) << 20;
    return w | PT_GUARD | (uint64_t)d | (uint64_t)a << 8 | (uint64_t)width << 37 | (uint64_t)c << 39 | (uint64_t)wrap << 50;
}

// HSETP2 Pa, Pb, Ra, Rb (register form, both halves), compare `cmp`, combined with predicate (p, pneg) by `bop`
static uint64_t hsetp2(int pa, int pb, int a, int b, int cmp, int p, bool pneg, int bop, bool hand) {
    return 0x5d20000000000000ull | PT_GUARD | (uint64_t)pb | (uint64_t)pa << 3 | (uint64_t)a << 8 | (uint64_t)b << 20 |
           (uint64_t)cmp << 35 | (uint64_t)p << 39 | (uint64_t)pneg << 42 | (uint64_t)bop << 45 | (uint64_t)hand << 49;
}

// LDC Rd, c[bank][Ra + off] (32-bit)
// R2P / P2R with an immediate mask (0x38f0 / 0x38e8): .CC (bit 40), the byte (bits 41-42), Ra, the mask in bits 20-38
static uint64_t r2p(bool cc, int byte, int a, uint32_t mask) {
    return 0x38f0000000000000ull | (uint64_t)cc << 40 | (uint64_t)byte << 41 | (uint64_t)(mask & 0x7FFFF) << 20 |
           (uint64_t)a << 8 | PT_GUARD | 0xFFull;
}
static uint64_t p2r(bool cc, int byte, int d, int a, uint32_t mask) {
    return 0x38e8000000000000ull | (uint64_t)cc << 40 | (uint64_t)byte << 41 | (uint64_t)(mask & 0x7FFFF) << 20 |
           (uint64_t)a << 8 | PT_GUARD | (uint64_t)d;
}
static uint64_t ldc(int d, int a, int bank, int off) {
    return 0xef90000000000000ull | PT_GUARD | (uint64_t)d | (uint64_t)a << 8 | (uint64_t)(uint16_t)off << 20 |
           (uint64_t)bank << 36 | 4ull << 48;
}

typedef struct {
    const char *name;
    uint64_t insn[6];  // 0-terminated
    const char *var;   // the register / predicate to check
    uint32_t expect;
} Case;

enum { LT = 1, GT = 4, AND = 0, OR = 1 };
#define LO 0x89ABCDEFu
#define HI 0x01234567u

static const Case *cases(int *n) {
    static Case c[64];
    int k = 0;
#define CASE(nm, var_, exp_, ...) c[k++] = (Case){nm, {__VA_ARGS__, 0}, var_, exp_}
    // I2I: .SAT compares in the source's signedness and saturates 32-bit destinations too
    CASE("I2I.U8.U32.SAT 0xffffffff", "r0", 0xFFu, mov32i(1, 0xFFFFFFFFu), i2i(0, 1, 0, false, 2, false, true, false, false));
    CASE("I2I.U16.U32.SAT 0x12345", "r0", 0xFFFFu, mov32i(1, 0x12345u), i2i(0, 1, 1, false, 2, false, true, false, false));
    CASE("I2I.S16.U32.SAT 0x80000000", "r0", 0x7FFFu, mov32i(1, 0x80000000u), i2i(0, 1, 1, true, 2, false, true, false, false));
    CASE("I2I.S32.U32.SAT 0x80000000", "r0", 0x7FFFFFFFu, mov32i(1, 0x80000000u), i2i(0, 1, 2, true, 2, false, true, false, false));
    CASE("I2I.U32.S32.SAT -1", "r0", 0u, mov32i(1, 0xFFFFFFFFu), i2i(0, 1, 2, false, 2, true, true, false, false));
    CASE("I2I.U32.S32.SAT 5", "r0", 5u, mov32i(1, 5u), i2i(0, 1, 2, false, 2, true, true, false, false));
    CASE("I2I.S8.S32.SAT -200", "r0", 0xFFFFFF80u, mov32i(1, (uint32_t)-200), i2i(0, 1, 0, true, 2, true, true, false, false));
    CASE("I2I.U16.S32.SAT 70000", "r0", 0xFFFFu, mov32i(1, 70000u), i2i(0, 1, 1, false, 2, true, true, false, false));
    CASE("I2I.S8.S32 0x1ff (zero-extended)", "r0", 0xFFu, mov32i(1, 0x1FFu), i2i(0, 1, 0, true, 2, true, false, false, false));
    CASE("I2I.S32.S32 |-5|", "r0", 5u, mov32i(1, (uint32_t)-5), i2i(0, 1, 2, true, 2, true, false, true, false));
    // SHF: hi:lo funnel; .W wraps the shift, otherwise it's clamped to the width (32, or 64 for .U64 / .S64)
    CASE("SHF.R.W 4", "r0", 0x789ABCDEu, mov32i(1, LO), mov32i(2, HI), mov32i(3, 4), shf(true, 0, 1, 3, 0, 2, 0, true));
    CASE("SHF.R.W 36 (wraps to 4)", "r0", 0x789ABCDEu, mov32i(1, LO), mov32i(2, HI), mov32i(3, 36), shf(true, 0, 1, 3, 0, 2, 0, true));
    CASE("SHF.R 40 (clamps to 32)", "r0", HI, mov32i(1, LO), mov32i(2, HI), mov32i(3, 40), shf(true, 0, 1, 3, 0, 2, 0, false));
    CASE("SHF.L 40 (clamps to 32)", "r0", LO, mov32i(1, LO), mov32i(2, HI), mov32i(3, 40), shf(false, 0, 1, 3, 0, 2, 0, false));
    CASE("SHF.L.W 8", "r0", 0x23456789u, mov32i(1, LO), mov32i(2, HI), mov32i(3, 8), shf(false, 0, 1, 3, 0, 2, 0, true));
    CASE("SHF.R.U64 imm 36", "r0", 0x00123456u, mov32i(1, LO), mov32i(2, HI), shf(true, 0, 1, -1, 36, 2, 2, false));
    CASE("SHF.R.U64 imm 4", "r0", 0x789ABCDEu, mov32i(1, LO), mov32i(2, HI), shf(true, 0, 1, -1, 4, 2, 2, false));
    CASE("SHF.R.U64 70 (clamps to 64)", "r0", 0u, mov32i(1, LO), mov32i(2, HI), mov32i(3, 70), shf(true, 0, 1, 3, 0, 2, 2, false));
    CASE("SHF.R.S64 40", "r0", 0xFFF00000u, mov32i(1, 0), mov32i(2, 0xF0000000u), mov32i(3, 40), shf(true, 0, 1, 3, 0, 2, 3, false));
    CASE("SHF.R.S64 70 (clamps to 64)", "r0", 0xFFFFFFFFu, mov32i(1, 0), mov32i(2, 0xF0000000u), mov32i(3, 70), shf(true, 0, 1, 3, 0, 2, 3, false));
    CASE("SHF.L.U64 40", "r0", 0xABCDEF00u, mov32i(1, LO), mov32i(2, HI), mov32i(3, 40), shf(false, 0, 1, 3, 0, 2, 2, false));
    CASE("SHF.L.U64 64", "r0", 0u, mov32i(1, LO), mov32i(2, HI), mov32i(3, 64), shf(false, 0, 1, 3, 0, 2, 2, false));
    CASE("SHF.L.U64.W 72 (wraps to 8)", "r0", 0x23456789u, mov32i(1, LO), mov32i(2, HI), mov32i(3, 72), shf(false, 0, 1, 3, 0, 2, 2, true));
    CASE("SHF.L.U64 imm 40", "r0", 0xABCDEF00u, mov32i(1, LO), mov32i(2, HI), shf(false, 0, 1, -1, 40, 2, 2, false));
    // HSETP2.H_AND: both halves are combined with the predicate, then ANDed; the second result is the complement
    CASE("HSETP2.GT.H_AND.AND !PT (first)", "p0", 0, mov32i(1, 0x3C003C00u), mov32i(2, 0), hsetp2(0, 1, 1, 2, GT, 7, true, AND, true));
    CASE("HSETP2.GT.H_AND.AND !PT (second)", "p1", 1, mov32i(1, 0x3C003C00u), mov32i(2, 0), hsetp2(0, 1, 1, 2, GT, 7, true, AND, true));
    CASE("HSETP2.GT.H_AND.OR PT (first)", "p0", 1, mov32i(1, 0x00003C00u), mov32i(2, 0), hsetp2(0, 1, 1, 2, GT, 7, false, OR, true));
    CASE("HSETP2.GT.H_AND.OR PT (second)", "p1", 0, mov32i(1, 0x00003C00u), mov32i(2, 0), hsetp2(0, 1, 1, 2, GT, 7, false, OR, true));
    CASE("HSETP2.LT.AND PT (per half)", "p1", 1, mov32i(1, 0xBC000000u), mov32i(2, 0), hsetp2(0, 1, 1, 2, LT, 7, false, AND, false));
    // LDC: the immediate is signed (TOTK's skinning reads a bone matrix's first rows at -32 / -16 from its register);
    // c3's words hold 0xC0000000 + their offset (the prelude)
    CASE("LDC c[3][r1 - 16]", "r0", 0xC0000020u, mov32i(1, 0x30u), ldc(0, 1, 3, -16));
    CASE("LDC c[3][r1 + 8]", "r0", 0xC0000018u, mov32i(1, 0x10u), ldc(0, 1, 3, 8));
    // R2P.CC sets the flags Z, S, C from bits 0-2 (#20); P2R.CC reads them back (overflow isn't tracked: bit 3 is 0)
    CASE("R2P.CC 0xa -> P2R.CC (S)", "r0", 0x2u, mov32i(1, 0xAu), r2p(true, 0, 1, 0xF), p2r(true, 0, 0, 255, 0xF));
    CASE("R2P.CC 0x5 -> P2R.CC (Z, C)", "r0", 0x5u, mov32i(1, 0x5u), r2p(true, 0, 1, 0xF), p2r(true, 0, 0, 255, 0xF));
    CASE("R2P.CC byte 1, 0x300 -> P2R.CC (Z, S)", "r0", 0x3u, mov32i(1, 0x300u), r2p(true, 1, 1, 0xF), p2r(true, 0, 0, 255, 0xF));
    CASE("R2P.CC mask 1, 0xf -> P2R.CC (Z only)", "r0", 0x1u, mov32i(1, 0xFu), r2p(true, 0, 1, 0x1), p2r(true, 0, 0, 255, 0xF));
    CASE("R2P 0x5 -> P2R (p0, p2)", "r0", 0x5u, mov32i(1, 0x5u), r2p(false, 0, 1, 0x7F), p2r(false, 0, 0, 255, 0x7F));
#undef CASE
    *n = k;
    return c;
}

// C++ stand-ins for the MSL built-ins the bodies use (only the ones these instructions emit)
static const char prelude[] =
    "#include <cstdint>\n#include <cstdio>\n#include <cstring>\n#include <cstdlib>\n"
    "typedef uint32_t uint; typedef uint64_t ulong;\n"
    "#define long int64_t\n"
    "template <class T, class U> static T as_type(U u) { static_assert(sizeof(T) == sizeof(U), \"size\"); T t; memcpy(&t, &u, sizeof t); return t; }\n"
    "template <class T> static T min(T a, T b) { return b < a ? b : a; }\n"
    "template <class T> static T max(T a, T b) { return a < b ? b : a; }\n"
    "template <class T> static T clamp(T x, T lo, T hi) { return min(max(x, lo), hi); }\n"
    "struct float2 { float x, y; };\n"
    "static float h2f(uint h) { _Float16 f; uint16_t b = (uint16_t)h; memcpy(&f, &b, 2); return (float)f; }\n"
    "static float2 hx(uint v, uint swz) { if (swz == 1u) { float f = as_type<float>(v); return {f, f}; }"
    " float2 h = {h2f(v & 0xFFFFu), h2f(v >> 16)}; return swz == 2u ? float2{h.x, h.x} : swz == 3u ? float2{h.y, h.y} : h; }\n"
    "static float2 operator-(float2 a) { return {-a.x, -a.y}; }\n"
    "struct uint4 { uint v[4]; uint &operator[](uint i) { return v[i]; } };\n"
    "static uint4 cb3[4096];\n"
    "static uint cbr(uint4 *cb, uint off) { return off >> 4 < 4096u ? cb[off >> 4][(off >> 2) & 3] : 0xDEADu; }\n"
    "static struct CbInit { CbInit() { for (uint i = 0; i < 4096 * 4; i++) cb3[i / 4][i % 4] = 0xC0000000u + i * 4; } } cb_init;\n"
    "static int fails;\n"
    "static void check(const char *name, uint got, uint want) {\n"
    "    if (got != want) { printf(\"FAIL %s: got 0x%08x, want 0x%08x\\n\", name, got, want); fails++; }\n"
    "}\n";

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <out.cpp>\n", argv[0]); return 2; }
    FILE *o = fopen(argv[1], "w");
    if (!o) { perror(argv[1]); return 2; }
    fputs(prelude, o);
    int n, bad = 0;
    const Case *cs = cases(&n);
    for (int i = 0; i < n; i++) {
        uint64_t prog[16];
        int m = 0;
        for (const uint64_t *w = cs[i].insn; *w; w++) {
            if (m % 4 == 0) prog[m++] = 0;  // scheduling word
            prog[m++] = *w;
        }
        if (m % 4 == 0) prog[m++] = 0;
        prog[m++] = EXIT;
        while (m % 4) prog[m++] = NOP;
        ShaderKey key = {0};
        ShaderInfo *s = maxwell_translate((const uint8_t *)prog, (size_t)m * 8, STAGE_COMPUTE, &key);
        const char *b = s->msl ? strstr(s->msl, "kernel void main0(") : NULL;
        b = b ? strstr(b, "{\n") : NULL;
        const char *e = b ? strstr(b, "  return;\n}") : NULL;
        if (!e) { printf("FAIL %s: translation: %s\n", cs[i].name, s->msl ? "unexpected layout" : s->error); bad++; continue; }
        fprintf(o, "static void case%d() %.*s  check(\"%s\", (uint)%s, 0x%08xu);\n}\n", i, (int)(e - b), b, cs[i].name, cs[i].var,
                cs[i].expect);
        maxwell_free(s);
    }
    fprintf(o, "int main() {\n");
    for (int i = 0; i < n; i++) fprintf(o, "    case%d();\n", i);
    fprintf(o, "    printf(\"%d cases, %%d failures\\n\", fails);\n    return fails != 0;\n}\n", n);
    fclose(o);
    return bad != 0;
}
