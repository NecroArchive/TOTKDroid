// SPDX-License-Identifier: GPL-3.0-or-later
// (part of this folder: LICENSE.md)

// TOTKDroid's stand-ins for what lsfg_chain.c took from Swap2-Native's replay (its vka.h): the device's function table
// (vk.*), the device, queue and its lock (V.*), a memory type finder, an image layout barrier and the log. TOTK's
// Vulkan functions are volk's globals and its device is g_vk (vulkan/vk.h); every submission is the render thread's.
#pragma once
#include <pthread.h>
#include <stdint.h>
#include "rt.h"
#include "../vk.h"

#define VKA_DECL(f) PFN_##f f;
#define LSFG_VK_FNS(X)                                                                                                    \
    X(vkAllocateCommandBuffers) X(vkAllocateDescriptorSets) X(vkAllocateMemory) X(vkBeginCommandBuffer)                   \
    X(vkBindBufferMemory) X(vkBindImageMemory) X(vkCmdBindDescriptorSets) X(vkCmdBindPipeline) X(vkCmdDispatch)           \
    X(vkCmdPipelineBarrier) X(vkCreateBuffer) X(vkCreateCommandPool) X(vkCreateComputePipelines)                          \
    X(vkCreateDescriptorPool) X(vkCreateDescriptorSetLayout) X(vkCreateImage) X(vkCreateImageView)                         \
    X(vkCreatePipelineLayout) X(vkCreateSampler) X(vkCreateShaderModule) X(vkDestroyBuffer) X(vkDestroyImage)             \
    X(vkDestroyImageView) X(vkDestroySampler) X(vkDestroyShaderModule) X(vkEndCommandBuffer) X(vkFreeMemory)              \
    X(vkGetBufferMemoryRequirements) X(vkGetDeviceProcAddr) X(vkGetImageMemoryRequirements) X(vkMapMemory)                 \
    X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkUpdateDescriptorSets)
typedef struct { LSFG_VK_FNS(VKA_DECL) } lsfg_vk_table;
static inline lsfg_vk_table *lsfg_vk(void) {
    static lsfg_vk_table t;
    static int done;
    if (!done) {
#define LSFG_VK_SET(f) t.f = f;
        LSFG_VK_FNS(LSFG_VK_SET)
#undef LSFG_VK_SET
        done = 1;
    }
    return &t;
}
#define vk (*lsfg_vk())

typedef struct { VkDevice dev; VkQueue q; uint32_t qf; VkDeviceSize ub_align; pthread_mutex_t qmu; } lsfg_v;
static inline lsfg_v *lsfg_dev(void) {
    static lsfg_v v = {.qmu = PTHREAD_MUTEX_INITIALIZER};
    v.dev = g_vk.dev, v.q = g_vk.queue, v.qf = g_vk.qfam, v.ub_align = g_vk.props.limits.minUniformBufferOffsetAlignment;
    return &v;
}
#define V (*lsfg_dev())

#define VLOG(...) (LOG("[lsfg] " __VA_ARGS__), LOG("\n"))
#define VRED(...) (LOG("[lsfg] ERROR: " __VA_ARGS__), LOG("\n"))

// A memory type of `bits` with all of `want` and none of `avoid` (UINT32_MAX: none)
static inline uint32_t vka_mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) && (f & want) == want && !(f & avoid)) return i;
    }
    return UINT32_MAX;
}

static inline void vka_img_barrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                              .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img, .subresourceRange = {aspect, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}
