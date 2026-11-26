// LARECOMP_MEMFD_PROBE=1 (a test, Android): can this driver give the guest heap as GPU memory through an exported file
// (VK_KHR_external_memory_fd: a dma-buf mapped at the heap's own address, exec.c vk_heap_place - after
// legendszaonandroid's VULKAN_BACKEND.md, MIT), where it has neither placed maps nor the host import (Mali, Xclipse)?
// For each host-visible memory type: 64 MB exported, its file mapped, the CPU's write and read speed through that map
// (the game runs on it: an uncached map crawls, as Mali's coherent type did in 0.2.3), and whether the GPU sees what the
// CPU wrote and the CPU what the GPU wrote. Once, at the device's start, before the game; the log says it all.
#ifdef __ANDROID__
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "vk.h"

#define PROBE_SIZE (64u << 20)

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// The GPU copies src[0, 4 KB) to dst and fills dst[4 KB, 8 KB) with 0x5a: true once done
static bool gpu_round(VkBuffer src, VkBuffer dst) {
    VkCommandPool pool;
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = g_vk.qfam};
    if (vkCreateCommandPool(g_vk.dev, &pi, NULL, &pool) != VK_SUCCESS) return false;
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    bool ok = vkAllocateCommandBuffers(g_vk.dev, &ai, &cb) == VK_SUCCESS;
    if (ok) {
        VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                       .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkBeginCommandBuffer(cb, &bi);
        VkBufferCopy cp = {0, 0, 4096};
        vkCmdCopyBuffer(cb, src, dst, 1, &cp);
        vkCmdFillBuffer(cb, dst, 4096, 4096, 0x5a5a5a5au);
        VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        vkEndCommandBuffer(cb);
        VkFence fence;
        VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        ok = vkCreateFence(g_vk.dev, &fi, NULL, &fence) == VK_SUCCESS;
        if (ok) {
            VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
            ok = vkQueueSubmit(g_vk.queue, 1, &si, fence) == VK_SUCCESS &&
                 vkWaitForFences(g_vk.dev, 1, &fence, VK_TRUE, 2000000000ull) == VK_SUCCESS;
            vkDestroyFence(g_vk.dev, fence, NULL);
        }
    }
    vkDestroyCommandPool(g_vk.dev, pool, NULL);
    return ok;
}

static void probe_type(uint32_t type, VkMemoryPropertyFlags flags, VkExternalMemoryHandleTypeFlagBits ht) {
    const char *kind = ht == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT ? "dma-buf" : "opaque";
    VkExternalMemoryBufferCreateInfo eb = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                           .handleTypes = ht};
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &eb, .size = PROBE_SIZE,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    VkBuffer buf = VK_NULL_HANDLE, out = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    int fd = -1;
    uint8_t *p = MAP_FAILED;
    const char *why = NULL;
    bool dedicated = false;  // (exported only as the buffer's own allocation: exec.c heap_export_ok)
    {
        VkPhysicalDeviceExternalBufferInfo q = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
                                                .usage = bci.usage, .handleType = ht};
        VkExternalBufferProperties ep = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
        vkGetPhysicalDeviceExternalBufferProperties(g_vk.phys, &q, &ep);
        VkExternalMemoryFeatureFlags f = ep.externalMemoryProperties.externalMemoryFeatures;
        dedicated = (f & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0;
        if (!(f & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT)) { why = "not exportable (features 0)"; goto done; }
    }
    if (vkCreateBuffer(g_vk.dev, &bci, NULL, &buf) != VK_SUCCESS) { why = "buffer"; goto done; }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g_vk.dev, buf, &mr);
    if (!(mr.memoryTypeBits & (1u << type))) { why = "the buffer can't use this type"; goto done; }
    VkMemoryDedicatedAllocateInfo ded = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .buffer = buf};
    VkExportMemoryAllocateInfo ex = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, .pNext = dedicated ? &ded : NULL,
                                     .handleTypes = ht};
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &ex, .allocationSize = mr.size,
                                .memoryTypeIndex = type};
    VkResult r = vkAllocateMemory(g_vk.dev, &mai, NULL, &mem);
    if (r != VK_SUCCESS) { why = "allocation"; goto done; }
    if (vkBindBufferMemory(g_vk.dev, buf, mem, 0) != VK_SUCCESS) { why = "bind"; goto done; }
    VkMemoryGetFdInfoKHR gi = {.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, .memory = mem,
                               .handleType = ht};
    if (vkGetMemoryFdKHR(g_vk.dev, &gi, &fd) != VK_SUCCESS || fd < 0) { why = "no file for it"; goto done; }
    p = mmap(NULL, PROBE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { why = "mmap of the file"; goto done; }
    // the CPU through the file's map: writes, then reads (what the game's code does all frame long)
    uint8_t *tmp = malloc(PROBE_SIZE);
    if (!tmp) { why = "memory"; goto done; }
    memset(tmp, 0x11, PROBE_SIZE);
    double t0 = now_s();
    for (int i = 0; i < 4; i++) memcpy(p, tmp, PROBE_SIZE);
    double t1 = now_s();
    volatile uint64_t sum = 0;
    for (int i = 0; i < 4; i++) {
        memcpy(tmp, p, PROBE_SIZE);
        sum += tmp[i * 4096];
    }
    double t2 = now_s();
    free(tmp);
    double wr = 4.0 * PROBE_SIZE / (t1 - t0) / 1e9, rd = 4.0 * PROBE_SIZE / (t2 - t1) / 1e9;
    // the GPU: sees the CPU's pattern (copied back out), and its own fill reaches the CPU's map
    // (the file's map isn't a vkMapMemory one, so nothing is flushed by hand: the result shows what the GPU gets as the
    // game would leave it. The copy and the fill land in the same memory: [0, 4 KB) to [8 MB, +4 KB), the fill after)
    for (int i = 0; i < 4096; i++) p[i] = (uint8_t)(i * 7 + 3);
    bool gpu_ok = false, cpu_sees = false, gpu_sees = false;
    {
        VkBufferCreateInfo vbi = bci;  // (a second buffer on the same memory at 8 MB: the copy's target)
        vbi.size = 1u << 20;
        if (vkCreateBuffer(g_vk.dev, &vbi, NULL, &out) == VK_SUCCESS && vkBindBufferMemory(g_vk.dev, out, mem, 8u << 20) == VK_SUCCESS)
            gpu_ok = gpu_round(buf, out);
    }
    if (gpu_ok) {
        gpu_sees = true;
        for (int i = 0; i < 4096 && gpu_sees; i++) gpu_sees = p[(8u << 20) + i] == (uint8_t)(i * 7 + 3);
        cpu_sees = true;
        for (int i = 0; i < 4096 && cpu_sees; i++) cpu_sees = p[(8u << 20) + 4096 + i] == 0x5a;
    }
    LOG("[memfd] %s%s, type %u (flags 0x%x%s%s): exported and mapped; CPU writes %.1f GB/s, reads %.1f GB/s; the GPU %s the "
        "CPU's bytes, the CPU %s the GPU's\n", kind, dedicated ? " (dedicated)" : "", type, (unsigned)flags,
        flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? " cached" : "", flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? " coherent" : "",
        wr, rd, !gpu_ok ? "(submission failed)" : gpu_sees ? "sees" : "does NOT see", !gpu_ok ? "-" : cpu_sees ? "sees" : "does NOT see");
done:
    if (why) LOG("[memfd] %s%s, type %u (flags 0x%x): failed at %s (errno %d)\n", kind, dedicated ? " (dedicated)" : "", type,
                 (unsigned)flags, why, errno);
    if (p != MAP_FAILED) munmap(p, PROBE_SIZE);
    if (fd >= 0) close(fd);
    if (out) vkDestroyBuffer(g_vk.dev, out, NULL);
    if (buf) vkDestroyBuffer(g_vk.dev, buf, NULL);
    if (mem) vkFreeMemory(g_vk.dev, mem, NULL);
}

#ifndef MREMAP_FIXED
#define MREMAP_FIXED 2
#endif
// Without a file (exec.c heap_move_map): memory of this type mapped by the driver, the map moved to another address with
// mremap - does it move, does the CPU run fast there, does the GPU see what the CPU wrote through it?
static void probe_move(uint32_t type, VkMemoryPropertyFlags flags) {
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = PROBE_SIZE,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer buf = VK_NULL_HANDLE, out = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    void *q = NULL, *at = MAP_FAILED;
    bool moved = false;
    const char *why = NULL;
    if (vkCreateBuffer(g_vk.dev, &bci, NULL, &buf) != VK_SUCCESS) { why = "buffer"; goto done; }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g_vk.dev, buf, &mr);
    if (!(mr.memoryTypeBits & (1u << type))) { why = "the buffer can't use this type"; goto done; }
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = type};
    if (vkAllocateMemory(g_vk.dev, &mai, NULL, &mem) != VK_SUCCESS) { why = "allocation"; goto done; }
    if (vkBindBufferMemory(g_vk.dev, buf, mem, 0) != VK_SUCCESS) { why = "bind"; goto done; }
    if (vkMapMemory(g_vk.dev, mem, 0, VK_WHOLE_SIZE, 0, &q) != VK_SUCCESS || !q) { why = "vkMapMemory"; goto done; }
    at = mmap(NULL, mr.size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);  // (the place to move it to)
    if (at == MAP_FAILED) { why = "a place to move it to"; goto done; }
    if (mremap(q, mr.size, mr.size, MREMAP_MAYMOVE | MREMAP_FIXED, at) != at) { why = "mremap"; goto done; }
    moved = true;
    uint8_t *p = at, *tmp = malloc(PROBE_SIZE);
    if (!tmp) { why = "memory"; goto done; }
    memset(tmp, 0x11, PROBE_SIZE);
    double t0 = now_s();
    for (int i = 0; i < 4; i++) memcpy(p, tmp, PROBE_SIZE);
    double t1 = now_s();
    for (int i = 0; i < 4; i++) memcpy(tmp, p, PROBE_SIZE);
    double t2 = now_s();
    free(tmp);
    for (int i = 0; i < 4096; i++) p[i] = (uint8_t)(i * 7 + 3);
    bool gpu_ok = false, gpu_sees = false;
    VkBufferCreateInfo vbi = bci;
    vbi.size = 1u << 20;
    if (vkCreateBuffer(g_vk.dev, &vbi, NULL, &out) == VK_SUCCESS && vkBindBufferMemory(g_vk.dev, out, mem, 8u << 20) == VK_SUCCESS)
        gpu_ok = gpu_round(buf, out);
    if (gpu_ok) {
        gpu_sees = true;
        for (int i = 0; i < 4096 && gpu_sees; i++) gpu_sees = p[(8u << 20) + i] == (uint8_t)(i * 7 + 3);
    }
    LOG("[memfd] the driver's map moved, type %u (flags 0x%x): moved; CPU writes %.1f GB/s, reads %.1f GB/s; the GPU %s the "
        "CPU's bytes\n", type, (unsigned)flags, 4.0 * PROBE_SIZE / (t1 - t0) / 1e9, 4.0 * PROBE_SIZE / (t2 - t1) / 1e9,
        !gpu_ok ? "(submission failed)" : gpu_sees ? "sees" : "does NOT see");
done:
    if (why) LOG("[memfd] the driver's map moved, type %u (flags 0x%x): failed at %s (errno %d)\n", type, (unsigned)flags, why, errno);
    if (out) vkDestroyBuffer(g_vk.dev, out, NULL);
    if (buf) vkDestroyBuffer(g_vk.dev, buf, NULL);
    if (moved) {  // (the driver's old range held while it unmaps it: then the moved map goes)
        void *hold = mmap(q, mr.size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
        if (mem) vkFreeMemory(g_vk.dev, mem, NULL);
        if (hold != MAP_FAILED) munmap(hold, mr.size);
        munmap(at, mr.size);
    } else {
        if (at != MAP_FAILED) munmap(at, mr.size);
        if (mem) vkFreeMemory(g_vk.dev, mem, NULL);
    }
}

// ---- the heap as an Android hardware buffer (exec.c heap_ahb_place). Some drivers export none of their own memory -
// the Mali-G720's stock one says "not exportable" for an opaque file and for a dma-buf, and won't move its map (#45,
// the POCO X7 Pro's log of 2026-11-13) - but every Android driver imports a gralloc buffer: a BLOB is a dma-buf
// underneath. Its file is taken out of the buffer's handle (AHardwareBuffer_sendHandleToUnixSocket: the public NDK has
// no getter for it) and mapped where the CPU wants it, and the buffer is imported into Vulkan for the GPU.
#include <android/hardware_buffer.h>
#include <fcntl.h>
#include <sys/socket.h>

// A BLOB of size bytes the CPU and the GPU both use, and its file (the handle's dma-buf): -1 if not (why)
// The CPU's use asked of gralloc: often, or rarely with LARECOMP_VK_AHB_RARELY=1 - a test only: asked automatically for
// the whole heap on a MediaTek gralloc (POCO X7 Pro), the phone restarted as the game started (2026-11-25)
int g_ahb_cpu_rarely = -1;
int vk_ahb_alloc(uint64_t size, AHardwareBuffer **out, const char **why) {
    *out = NULL;
    if (size > 0xFFFFFFFFull) { *why = "too big for a BLOB"; return -1; }
    AHardwareBuffer_Desc d = {.width = (uint32_t)size, .height = 1, .layers = 1, .format = AHARDWAREBUFFER_FORMAT_BLOB,
                              .usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER};
    if (g_ahb_cpu_rarely < 0) g_ahb_cpu_rarely = getenv("LARECOMP_VK_AHB_RARELY") && atoi(getenv("LARECOMP_VK_AHB_RARELY")) != 0;
    d.usage |= g_ahb_cpu_rarely ? AHARDWAREBUFFER_USAGE_CPU_READ_RARELY | AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY
                                : AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    AHardwareBuffer *ahb = NULL;
    int e = AHardwareBuffer_allocate(&d, &ahb);
    if (e || !ahb) { errno = -e; *why = "AHardwareBuffer_allocate"; return -1; }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv)) { AHardwareBuffer_release(ahb); *why = "socketpair"; return -1; }
    int fd = -1;
    e = AHardwareBuffer_sendHandleToUnixSocket(ahb, sv[0]);
    if (!e) {  // (the handle's ints in the data, its fds as SCM_RIGHTS)
        // The buffer's memory: the file opened for reading and writing that is at least size bytes long, else the first
        // one. Qualcomm's gralloc hands it first; a MediaTek one (POCO X7 Pro, Mali-G720) gave a file mmap refused with
        // EACCES, so every file is looked at - and logged once, for a report.
        char data[1024], ctl[CMSG_SPACE(16 * sizeof(int))];
        struct iovec iov = {data, sizeof data};
        struct msghdr m = {.msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl, .msg_controllen = sizeof ctl};
        int all[16], nall = 0, pick = -1;
        if (recvmsg(sv[1], &m, MSG_CMSG_CLOEXEC) >= 0) {
            for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) {
                if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
                int n = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
                int *fds = (int *)CMSG_DATA(c);
                for (int i = 0; i < n; i++) {
                    if (nall < 16) all[nall++] = fds[i];
                    else close(fds[i]);
                }
            }
        }
        static int told;
        char desc[512];
        int dn = 0;
        for (int i = 0; i < nall; i++) {
            off_t len = lseek(all[i], 0, SEEK_END);
            lseek(all[i], 0, SEEK_SET);
            int fl = fcntl(all[i], F_GETFL);
            bool rw = fl >= 0 && (fl & O_ACCMODE) == O_RDWR;
            if (dn < (int)sizeof desc - 64)
                dn += snprintf(desc + dn, sizeof desc - dn, "%s#%d %lld bytes %s", i ? ", " : "", i, (long long)len,
                               fl < 0 ? "?" : rw ? "rw" : (fl & O_ACCMODE) == O_RDONLY ? "ro" : "wo");
            if (fd < 0 && rw && len >= (off_t)size) fd = all[i], pick = i;
        }
        if (fd < 0 && nall) fd = all[0], pick = 0;
        for (int i = 0; i < nall; i++) if (all[i] != fd) close(all[i]);
        if (!told++) LOG("[vk] Android hardware buffer of %llu bytes: %d file(s) in its handle: %s - using #%d\n",
                         (unsigned long long)size, nall, nall ? desc : "none", pick);
    } else errno = -e;
    close(sv[0]);
    close(sv[1]);
    if (fd < 0) { AHardwareBuffer_release(ahb); *why = e ? "AHardwareBuffer_sendHandleToUnixSocket" : "no file in its handle"; return -1; }
    *out = ahb;
    return fd;
}

// The buffer imported into Vulkan as a VkBuffer of size bytes over all its memory: false if not (why)
bool vk_ahb_import(AHardwareBuffer *ahb, uint64_t size, VkBufferUsageFlags usage, bool bda, VkBuffer *pbuf,
                   VkDeviceMemory *pmem, int *ptype, const char **why) {
    *pbuf = VK_NULL_HANDLE, *pmem = VK_NULL_HANDLE, *ptype = -1;
    if (!g_vk.mem_ahb) { *why = "no VK_ANDROID_external_memory_android_hardware_buffer"; return false; }
    VkAndroidHardwareBufferPropertiesANDROID hp = {.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    if (vkGetAndroidHardwareBufferPropertiesANDROID(g_vk.dev, ahb, &hp) != VK_SUCCESS) { *why = "its properties"; return false; }
    if (hp.allocationSize < size) { *why = "smaller than asked"; return false; }
    VkExternalMemoryBufferCreateInfo eb = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                           .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &eb, .size = size, .usage = usage};
    VkBuffer buf;
    if (vkCreateBuffer(g_vk.dev, &bi, NULL, &buf) != VK_SUCCESS) { *why = "the buffer"; return false; }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g_vk.dev, buf, &mr);
    uint32_t bits = hp.memoryTypeBits & mr.memoryTypeBits;
    int type = -1;
    for (int i = 0; i < 32 && type < 0; i++) if (bits & (1u << i)) type = i;
    if (type < 0) { vkDestroyBuffer(g_vk.dev, buf, NULL); *why = "no memory type for the buffer"; return false; }
    VkMemoryAllocateFlagsInfo af = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkImportAndroidHardwareBufferInfoANDROID imp = {.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
                                                    .pNext = bda ? &af : NULL, .buffer = ahb};
    VkMemoryDedicatedAllocateInfo ded = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .pNext = &imp, .buffer = buf};
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &ded, .allocationSize = hp.allocationSize,
                               .memoryTypeIndex = (uint32_t)type};
    VkDeviceMemory mem;
    VkResult r = vkAllocateMemory(g_vk.dev, &ai, NULL, &mem);
    if (r != VK_SUCCESS) { vkDestroyBuffer(g_vk.dev, buf, NULL); *why = "the import"; errno = -r; return false; }
    if (vkBindBufferMemory(g_vk.dev, buf, mem, 0) != VK_SUCCESS) {
        vkDestroyBuffer(g_vk.dev, buf, NULL);
        vkFreeMemory(g_vk.dev, mem, NULL);
        *why = "binding it";
        return false;
    }
    *pbuf = buf, *pmem = mem, *ptype = type;
    return true;
}

// Whether the GPU sees what the CPU wrote through map p (over buf's memory mem, from its start) and the CPU what the GPU
// wrote: 8 KB at off scribbled and cleared again (vk_heap_place before the game writes there; the probe)
bool vk_ahb_coherent(uint8_t *p, VkBuffer buf, VkDeviceMemory mem, uint64_t off, VkBufferUsageFlags usage, const char **why) {
    VkBufferCreateInfo vbi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 1u << 20, .usage = usage};
    VkExternalMemoryBufferCreateInfo eb = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                           .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    vbi.pNext = &eb;
    VkBuffer out = VK_NULL_HANDLE;
    bool ok = false;
    // Three rounds, each the strict case for a cache the GPU doesn't snoop: the CPU's bytes written just before the
    // GPU reads them (still in the CPU's cache), and the bytes the GPU will write read by the CPU just before (stale
    // lines in its cache). One round alone passed on a MediaTek gralloc's cached memory once in two runs (POCO X7 Pro):
    // whether the lines had left the cache by chance.
    if (vkCreateBuffer(g_vk.dev, &vbi, NULL, &out) != VK_SUCCESS || vkBindBufferMemory(g_vk.dev, out, mem, off) != VK_SUCCESS)
        *why = "a second buffer on its memory";
    for (int round = 0; round < 3 && !*why; round++) {
        volatile uint8_t sink = 0;
        for (int i = 0; i < 4096; i++) sink ^= p[off + 4096 + i];  // (into the CPU's cache)
        (void)sink;
        for (int i = 0; i < 4096; i++) p[i] = (uint8_t)(i * 7 + 3 + round);
        if (!gpu_round(buf, out)) { *why = "the GPU's round (submission)"; break; }
        bool gpu_sees = true, cpu_sees = true;
        for (int i = 0; i < 4096 && gpu_sees; i++) gpu_sees = p[off + i] == (uint8_t)(i * 7 + 3 + round);
        for (int i = 0; i < 4096 && cpu_sees; i++) cpu_sees = p[off + 4096 + i] == 0x5a;
        if (!gpu_sees || !cpu_sees) { *why = !gpu_sees ? "the GPU doesn't see the CPU's bytes" : "the CPU doesn't see the GPU's bytes"; break; }
        memset(p + off, 0, 8192);  // (the next round's GPU writes over zeros again)
        ok = round == 2;
    }
    if (out) vkDestroyBuffer(g_vk.dev, out, NULL);
    memset(p, 0, 4096);
    memset(p + off, 0, 8192);
    return ok;
}

static void probe_ahb(void) {
    // (64 MB only: asking gralloc for BLOBs of 256 MB to 2 GB one after another restarted the POCO's system_server,
    // 2026-11-14)
    AHardwareBuffer *ahb = NULL;
    const char *why = NULL;
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    uint8_t *p = MAP_FAILED;
    int type = -1;
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    int fd = vk_ahb_alloc(PROBE_SIZE, &ahb, &why);
    if (fd < 0) goto done;
    p = mmap(NULL, PROBE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { why = "mmap of its file"; goto done; }
    if (!vk_ahb_import(ahb, PROBE_SIZE, usage, false, &buf, &mem, &type, &why)) goto done;
    uint8_t *tmp = malloc(PROBE_SIZE);
    if (!tmp) { why = "memory"; goto done; }
    memset(tmp, 0x11, PROBE_SIZE);
    double t0 = now_s();
    for (int i = 0; i < 4; i++) memcpy(p, tmp, PROBE_SIZE);
    double t1 = now_s();
    for (int i = 0; i < 4; i++) memcpy(tmp, p, PROBE_SIZE);
    double t2 = now_s();
    free(tmp);
    bool ok = vk_ahb_coherent(p, buf, mem, 8u << 20, usage, &why);
    LOG("[memfd] Android hardware buffer, type %d: allocated, its file mapped, imported; CPU writes %.1f GB/s, reads %.1f GB/s; "
        "%s\n", type, 4.0 * PROBE_SIZE / (t1 - t0) / 1e9, 4.0 * PROBE_SIZE / (t2 - t1) / 1e9,
        ok ? "the GPU sees the CPU's bytes, the CPU sees the GPU's" : why);
    why = NULL;
done:
    if (why) LOG("[memfd] Android hardware buffer: failed at %s (errno %d)\n", why, errno);
    if (buf) vkDestroyBuffer(g_vk.dev, buf, NULL);
    if (mem) vkFreeMemory(g_vk.dev, mem, NULL);
    if (p != MAP_FAILED) munmap(p, PROBE_SIZE);
    if (fd >= 0) close(fd);
    if (ahb) AHardwareBuffer_release(ahb);
}

void vk_memfd_probe(void) {
    extern bool vk_heap_file_asked(void);  // (and whenever the heap goes through the file: its memory types in the log)
    if (!getenv("LARECOMP_MEMFD_PROBE") && !vk_heap_file_asked()) return;
    if (g_vk.mem_ahb) probe_ahb();
    if (!g_vk.mem_fd) { LOG("[memfd] no VK_KHR_external_memory_fd on this driver\n"); return; }
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
        if (!(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        probe_move(i, f);  // (no file at all: exec.c heap_move_map)
        probe_type(i, f, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
        if (g_vk.mem_dmabuf) probe_type(i, f, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);  // (the other file kind, #234)
    }
}
#endif
