#!/usr/bin/env python3
"""Emit a host test of the production low-RAM threshold (no phone required).

Compile the generated C file with clang -D_GNU_SOURCE, then run it.
Each case uses a fresh child because vk_low_ram caches the detection.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'runtime/nvn/vulkan/exec.c').read_text(encoding='utf-8')
start = source.index('bool vk_low_ram(void) {')
end = source.index('\n#define CHUNK_SIZE', start)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
static long mem_kb;
static FILE *meminfo(const char *path, const char *mode) {
    (void)path; (void)mode;
    FILE *f = tmpfile(); assert(f);
    fprintf(f, "MemTotal: %ld kB\n", mem_kb); rewind(f); return f;
}
#define fopen meminfo
#define LOG(...) ((void)0)
'''
tests = r'''
int main(void) {
    const struct { long kb; bool expected; const char *override; unsigned cache_mb; } cases[] = {
        {3700000, true, NULL, 301}, {4194304, true, NULL, 341}, {4194305, false, NULL, 768},
        {5700000, false, NULL, 768}, {7700000, false, NULL, 768}, {0, false, NULL, 768},
        {12000000, true, "1", 768}, {3700000, false, "0", 768},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            mem_kb = cases[i].kb;
            if (cases[i].override) setenv("LARECOMP_LOW_RAM", cases[i].override, 1);
            else unsetenv("LARECOMP_LOW_RAM");
            unsetenv("LARECOMP_VC_BUDGET_MB");
            assert(vk_low_ram() == cases[i].expected);
            assert((vc_budget() >> 20) == cases[i].cache_mb);
            _exit(0);
        }
        int status; assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    puts("PASS: low RAM and reduced cache only through 4 GiB; 6/8 GB keep 768 MB; diagnostic overrides work");
}
'''
out = root / 'build/botw-port-20261108/low-ram-test.c'
out.parent.mkdir(parents=True, exist_ok=True)
cache_start = source.index('static uint64_t vc_budget(void) {')
cache_end = source.index('\n#define VC_BUDGET', cache_start)
out.write_text(prefix + source[start:end] + source[cache_start:cache_end] + tests, encoding='utf-8')
print(out)
