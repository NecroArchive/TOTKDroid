#!/usr/bin/env python3
"""Emit host tests from production AArch64 recovery, sampler and low-RAM mip code.

Run this generator, then compile build/botw-port-20261108/recent-port-test.c with
clang -D_GNU_SOURCE -D__ANDROID__ -Iruntime -Iandroid/deps/out/include.
No GPU or game files are required; Vulkan calls and the signal context are stubs.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
android = (root / 'runtime/platform/android.c').read_text(encoding='utf-8')
textures = (root / 'runtime/nvn/vulkan/textures.c').read_text(encoding='utf-8')

def function(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>
#include "nvn/nvn.h"
static uint32_t code[1024];
#undef GUEST_LO
#undef GUEST_HI
#define GUEST_LO ((uint64_t)(uintptr_t)code)
#define GUEST_HI (GUEST_LO + sizeof code)
typedef struct { struct { uint64_t regs[31], pc; } uc_mcontext; } ucontext_t;
typedef struct { const char *dli_fname; void *dli_fbase; } Dl_info;
static int dladdr(const void *p, Dl_info *di) { (void)p; (void)di; return 0; }
static NvnSamplerDesc seen;
static int sampler_count;
static VkSampler make_sampler(const NvnSamplerDesc *d, bool compare) {
    (void)compare; seen = *d;
    return (VkSampler)(uintptr_t)++sampler_count;
}
static bool vk_low_ram(void) { return true; }
enum { RS_NONE = 0 };
#undef LOG
#define LOG(...) ((void)0)
static int mip_skip(unsigned w, unsigned h, unsigned count, unsigned flags, unsigned group, unsigned type_) {
    struct { unsigned flags; struct { unsigned levels; } layout; } tex = {flags, {count}}, *t = &tex;
    struct { unsigned skip, rs_group; } backend = {0,group}, *b = &backend;
    unsigned tw = w, th = h, levels = count, samples = 1;
    VkImageType type = (VkImageType)type_;
    bool depthfmt = false;
'''
start = textures.index('    {   // Low RAM (exec.c vk_low_ram')
end = textures.index('    VkImageCreateInfo ci =', start)
source = prefix + textures[start:end] + r'''
    if (b->skip) assert(tw == w/2 && th == h/2 && levels == count-1);
    else assert(tw == w && th == h && levels == count);
    return b->skip;
}
'''
source += function(android, 'static bool null_read(') + '\n'
source += function(android, 'static bool null_call_native(') + '\n'
source += function(textures, 'VkSampler vk_sampler_nearest(') + '\n'
source += r'''
int main(void) {
    unsetenv("LARECOMP_NO_NULL_READS"); unsetenv("LARECOMP_NO_NULL_CALLS");
    setenv("LARECOMP_HALF_TEXTURES", "1", 1);
    const uint32_t loads[] = {0xf9400000u, 0xb9400000u, 0x39400000u, 0x79400000u, 0xb9800000u, 0xa9400400u};
    ucontext_t uc;
    for (unsigned i=0;i<sizeof loads/sizeof *loads;i++) {
        memset(&uc, 0x5a, sizeof uc); code[0] = loads[i];
        assert(null_read(&uc, 0x20, GUEST_LO));
        assert(uc.uc_mcontext.regs[0] == 0 && uc.uc_mcontext.pc == GUEST_LO + 4);
        if (i == 5) assert(uc.uc_mcontext.regs[1] == 0);
    }
    const uint32_t rejected[] = {0xf9000000u, 0xf8400c00u, 0x3dc00000u, 0xbd400000u, 0xf9800000u, 0xd503201fu};
    for (unsigned i=0;i<sizeof rejected/sizeof *rejected;i++) {
        memset(&uc, 0x5a, sizeof uc); ucontext_t saved=uc; code[0]=rejected[i];
        assert(!null_read(&uc, 0x20, GUEST_LO) && !memcmp(&uc,&saved,sizeof uc));
    }
    code[0]=loads[0];
    assert(!null_read(&uc,0x10000,GUEST_LO));
    assert(!null_read(&uc,0,GUEST_LO+1));
    uc.uc_mcontext.regs[30]=GUEST_LO+64;
    assert(null_call_native(&uc,0) && uc.uc_mcontext.pc==GUEST_LO+64 && uc.uc_mcontext.regs[0]==0);
    assert(!null_call_native(&uc,0x10000));
    uc.uc_mcontext.regs[30]=0x800;
    assert(!null_call_native(&uc,0));

    NvnSamplerDesc d={0}; d.mag_filter=1; d.min_filter=5; d.max_aniso=16;
    d.lod_min=2; d.lod_max=7; d.wrap[0]=1;
    VkSampler first=vk_sampler_nearest(&d,false);
    assert(seen.mag_filter==0 && seen.min_filter==2 && seen.max_aniso==1 && seen.lod_min==2 && seen.lod_max==7);
    assert(vk_sampler_nearest(&d,false)==first && sampler_count==1);
    d.wrap[0]=7; // same descriptor address, new contents: never return the old sampler
    assert(vk_sampler_nearest(&d,false)!=first && sampler_count==2 && seen.wrap[0]==7);
    assert(vk_sampler_nearest(&d,true)!=(VkSampler)(uintptr_t)2 && sampler_count==3);

    assert(mip_skip(256,256,9,0,0,VK_IMAGE_TYPE_2D)==1);
    assert(mip_skip(256,256,9,8,0,VK_IMAGE_TYPE_2D)==0);
    assert(mip_skip(260,260,1,0x40,0,VK_IMAGE_TYPE_2D)==0);
    assert(mip_skip(256,256,9,0,1,VK_IMAGE_TYPE_2D)==0);
    assert(mip_skip(64,64,7,0,0,VK_IMAGE_TYPE_2D)==0);
    assert(mip_skip(256,256,1,0,0,VK_IMAGE_TYPE_2D)==0);
    assert(mip_skip(256,256,9,0,0,VK_IMAGE_TYPE_3D)==0);
    puts("PASS: AArch64 recovery rejects stores/SIMD/writeback; sampler rewrites; low-RAM mip exclusions");
}
'''
out = root / 'build/botw-port-20261108/recent-port-test.c'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(source, encoding='utf-8')
print(out)
