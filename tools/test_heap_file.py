#!/usr/bin/env python3
"""Emit a host regression for the production shared-file heap placement function.

Compile build/botw-port-20261108/heap-file-test.c with clang -D__ANDROID__
-Iruntime -Iandroid/deps/out/include, then run it. Vulkan and mmap are mocked:
this checks selection/fallback/cleanup, not a driver's actual memory visibility.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'runtime/nvn/vulkan/exec.c').read_text(encoding='utf-8')
start = source.index('bool vk_heap_file_asked(void) {')
end = source.index('\n#undef NOT_PLACED', start)
body = source[start:end] + '\n#undef NOT_PLACED\n'
prefix = r'''
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#define LOG(...) ((void)0)
#define HEAP_BASE 0x340000000ull
#define HEAP_MAX (4ull << 30)
static struct {
    VkDevice dev; VkPhysicalDevice phys; VkPhysicalDeviceProperties props;
    bool mem_fd, mem_dmabuf, map_placed, bda; uint64_t placed_align;
} g_vk;
Settings g_settings;
static VkBuffer gmem_buf;
static VkDeviceMemory gmem_mem;
static uint64_t gmem_size, gmem_bda;
static bool gmem_placed, gcopy_mode, gwin_mode;
static unsigned creates, destroys, allocations, frees, tracking;
static int scenario;
static VkExternalMemoryHandleTypeFlags handle, exported;
void vk_async_drain(void) {}
static void vk_wait_ready(void) {}
static void plat_write_tracking_enable(uint64_t a, uint64_t b) { (void)a;(void)b;tracking++; }
static void plat_reset_guest(uint64_t a,uint64_t b) { (void)a;(void)b; }
bool vk_turnip_path_on(int setting) { return setting > 0; }
static bool gmem_bda_wanted(uint64_t size) { (void)size;return false; }
static bool gaddr_write(uint64_t address) { (void)address;return false; }
static VkResult vkCreateBuffer(VkDevice d,const VkBufferCreateInfo *bi,const void *a,VkBuffer *buf) {
    (void)d;(void)a; creates++;
    handle = ((const VkExternalMemoryBufferCreateInfo *)bi->pNext)->handleTypes;
    if (scenario==2 && handle==VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT) return VK_ERROR_FEATURE_NOT_PRESENT;
    *buf=(VkBuffer)(uintptr_t)creates;return VK_SUCCESS;
}
static void vkGetBufferMemoryRequirements(VkDevice d,VkBuffer b,VkMemoryRequirements *mr) {
    (void)d;(void)b; *mr=(VkMemoryRequirements){.size=32ull<<20,.alignment=65536,.memoryTypeBits=2};
    if ((scenario==1 && handle==VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT) || scenario==3) mr->memoryTypeBits=1;
}
static void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *mp) {
    (void)p; memset(mp,0,sizeof *mp);mp->memoryTypeCount=2;
    mp->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    mp->memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
}
static void vkGetPhysicalDeviceProperties2(VkPhysicalDevice p,VkPhysicalDeviceProperties2 *props) { (void)p;(void)props; }
static VkResult vkAllocateMemory(VkDevice d,const VkMemoryAllocateInfo *ai,const void *a,VkDeviceMemory *m) {
    (void)d;(void)a; allocations++;
    exported=((const VkExportMemoryAllocateInfo *)ai->pNext)->handleTypes;
    assert(exported==handle && ai->memoryTypeIndex==1);
    if (scenario==4) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *m=(VkDeviceMemory)(uintptr_t)1;return VK_SUCCESS;
}
static VkResult vkBindBufferMemory(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize o) { (void)d;(void)b;(void)m;(void)o;return VK_SUCCESS; }
static VkResult vkGetMemoryFdKHR(VkDevice d,const VkMemoryGetFdInfoKHR *gi,int *fd) {
    (void)d;assert(gi->handleType==exported);*fd=99;return VK_SUCCESS;
}
static void *fake_mmap(void *p,size_t n,int prot,int flags,int fd,long offset) {
    (void)n;(void)prot;(void)offset;assert(fd==99 && (flags&MAP_SHARED) && (flags&MAP_FIXED));
    return scenario==5 ? MAP_FAILED : p;
}
static int fake_close(int fd) { assert(fd==99);return 0; }
static void vkDestroyBuffer(VkDevice d,VkBuffer b,const void *a) { (void)d;(void)b;(void)a;destroys++; }
static void vkFreeMemory(VkDevice d,VkDeviceMemory m,const void *a) { (void)d;(void)m;(void)a;frees++; }
static VkResult vkMapMemory2KHR(VkDevice d,const VkMemoryMapInfoKHR *mi,void **p) { (void)d;(void)mi;(void)p;abort(); }
static uint64_t vkGetBufferDeviceAddress(VkDevice d,const VkBufferDeviceAddressInfo *bi) { (void)d;(void)bi;abort(); }
#define mmap fake_mmap
#define close fake_close
'''
tests = r'''
static void reset(int mode) {
    scenario=mode;creates=destroys=allocations=frees=tracking=0;
    gmem_buf=VK_NULL_HANDLE;gmem_mem=VK_NULL_HANDLE;gmem_size=0;gmem_bda=0;
    gmem_placed=false;gcopy_mode=true;gwin_mode=false;
    memset(&g_vk,0,sizeof g_vk);g_vk.mem_fd=true;g_vk.mem_dmabuf=true;
    g_vk.props.limits.maxStorageBufferRange=16u<<20;
    g_settings.heap_file=true;g_settings.gpu_heap=-1;
    setenv("LARECOMP_HEAP_ANY_RAM","1",1);setenv("LARECOMP_VK_NO_PLACED","1",1);
    unsetenv("LARECOMP_VK_MEMFD");unsetenv("LARECOMP_VK_MEMFD_DMABUF");unsetenv("LARECOMP_VK_MEMFD_TYPE");
}
int main(void) {
    reset(0);assert(vk_heap_place(32u<<20) && creates==1 && exported==VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
    assert(gmem_placed && !gcopy_mode && gwin_mode);
    reset(1);assert(vk_heap_place(32u<<20) && creates==2 && destroys==1 && exported==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
    reset(2);assert(vk_heap_place(32u<<20) && creates==2 && exported==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
    reset(3);assert(!vk_heap_place(32u<<20) && creates==2 && destroys==2 && !allocations && tracking==1);
    reset(4);assert(!vk_heap_place(32u<<20) && destroys==1 && !frees && tracking==1);
    reset(5);assert(!vk_heap_place(32u<<20) && destroys==1 && frees==1 && tracking==1);
    reset(0);g_vk.mem_fd=false;assert(!vk_heap_place(32u<<20) && !creates && tracking==1);
    reset(0);g_settings.heap_file=false;assert(!vk_heap_place(32u<<20) && !creates && tracking==1);
    reset(0);setenv("LARECOMP_VK_MEMFD","0",1);assert(!vk_heap_place(32u<<20) && !creates);
    reset(0);setenv("LARECOMP_VK_MEMFD_DMABUF","1",1);assert(vk_heap_place(32u<<20) && creates==1 && exported==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
    puts("PASS: 10 shared-file heap cases (opaque, DMA-BUF, unsupported drivers, allocation/map failure, settings)");
}
'''
out = root / 'build/botw-port-20261108/heap-file-test.c'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(prefix + body + tests, encoding='utf-8')
print(out)
