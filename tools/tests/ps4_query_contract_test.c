/* Exercise production ICD capabilities and fail-closed queries. No GPU calls. */
#include "vk_ps4_internal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned logs, releases, frees;
void vk_ps4_log(const char *format, ...) { (void)format; ++logs; }
void vk_ps4_log_raw(const char *message) { (void)message; }
void vk_ps4_log_open(const char *path) { (void)path; }
bool vk_ps4_log_is_open(void) { return true; }
void *vk_ps4_alloc_zero(const VkAllocationCallbacks *alloc, size_t size, size_t align) {
    (void)alloc; (void)align; return calloc(1, size);
}
void vk_ps4_free(const VkAllocationCallbacks *alloc, void *ptr) {
    (void)alloc; ++frees; free(ptr);
}
void sceGnmDirectMemoryRelease(GnmDirectMemory *mem) {
    assert(mem->allocated); ++releases;
}

int main(void) {
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vk_ps4_CreateInstance(&ici, NULL, &instance) == VK_SUCCESS);
    uint32_t count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vk_ps4_EnumeratePhysicalDevices(instance, &count, &physical) == VK_SUCCESS);
    VkPhysicalDeviceProperties properties;
    vk_ps4_GetPhysicalDeviceProperties(physical, &properties);
    assert(properties.limits.timestampComputeAndGraphics == VK_FALSE);
    assert(properties.limits.timestampPeriod == 0.0f);
    VkPhysicalDeviceFeatures features;
    vk_ps4_GetPhysicalDeviceFeatures(physical, &features);
    assert(features.occlusionQueryPrecise == VK_FALSE);
    assert(features.pipelineStatisticsQuery == VK_FALSE);
    VkPhysicalDeviceHostQueryResetFeatures resetFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        .hostQueryReset = VK_TRUE
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &resetFeature
    };
    vk_ps4_GetPhysicalDeviceFeatures2(physical, &features2);
    assert(!resetFeature.hostQueryReset && !features2.features.occlusionQueryPrecise);
    vk_ps4_GetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    assert(count == VK_PS4_NUM_QUEUE_FAMILIES);
    VkQueueFamilyProperties families[VK_PS4_NUM_QUEUE_FAMILIES];
    memset(families, 0x7f, sizeof(families));
    vk_ps4_GetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    for (unsigned i = 0; i < count; ++i) assert(families[i].timestampValidBits == 0);
    count = 1;
    families[0].timestampValidBits = 64;
    vk_ps4_GetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    assert(count == 1 && families[0].timestampValidBits == 0);

    VkPs4Device dev = {0};
    VkQueryPoolCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                .queryCount = 2 };
    const VkQueryType types[] = {VK_QUERY_TYPE_TIMESTAMP, VK_QUERY_TYPE_OCCLUSION,
                                 VK_QUERY_TYPE_PIPELINE_STATISTICS};
    for (unsigned i = 0; i < sizeof(types)/sizeof(types[0]); ++i) {
        ci.queryType = types[i];
        VkQueryPool result = (VkQueryPool)(uintptr_t)0x1234;
        assert(vk_ps4_CreateQueryPool((VkDevice)&dev, &ci, NULL, &result) == VK_ERROR_FEATURE_NOT_PRESENT);
        assert(result == VK_NULL_HANDLE);
    }
    VkQueryPool result = (VkQueryPool)(uintptr_t)0x1234;
    ci.queryCount = 0;
    assert(vk_ps4_CreateQueryPool((VkDevice)&dev, &ci, NULL, &result) == VK_ERROR_INITIALIZATION_FAILED);
    assert(result == VK_NULL_HANDLE);

    /* Unsupported paths must not dereference invalid pool payloads, produce
       synthetic zero values/availability, or wait for GPU work that cannot run. */
    VkQueryPool invalid = (VkQueryPool)(uintptr_t)1;
    uint64_t output[4] = {11, 22, 33, 44};
    assert(vk_ps4_GetQueryPoolResults((VkDevice)&dev, invalid, UINT32_MAX, 2,
        sizeof(output), output, UINT64_MAX, VK_QUERY_RESULT_WAIT_BIT |
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(output[0] == 11 && output[1] == 22 && output[2] == 33 && output[3] == 44);
    vk_ps4_ResetQueryPoolEXT((VkDevice)&dev, invalid, UINT32_MAX, UINT32_MAX);
    VkPs4CommandBuffer *cmd = calloc(1, sizeof(*cmd));
    assert(cmd);
    uint32_t words[8];
    for (unsigned i = 0; i < 8; ++i) words[i] = 0xdeadbeef;
    cmd->gnm_cmd.beginptr = cmd->gnm_cmd.cmdptr = words;
    cmd->gnm_cmd.endptr = words + 8;
#define REJECTS(call) do { cmd->recording_error = VK_SUCCESS; call; \
    assert(cmd->recording_error == VK_ERROR_FEATURE_NOT_PRESENT); \
    assert(cmd->gnm_cmd.cmdptr == words); } while (0)
    REJECTS(vk_ps4_CmdWriteTimestamp((VkCommandBuffer)cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, invalid, 0));
    REJECTS(vk_ps4_CmdWriteTimestamp((VkCommandBuffer)cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, invalid, 0));
    REJECTS(vk_ps4_CmdResetQueryPool((VkCommandBuffer)cmd, invalid, UINT32_MAX, UINT32_MAX));
    REJECTS(vk_ps4_CmdBeginQuery((VkCommandBuffer)cmd, invalid, 0, 0));
    REJECTS(vk_ps4_CmdEndQuery((VkCommandBuffer)cmd, invalid, 0));
    REJECTS(vk_ps4_CmdCopyQueryPoolResults((VkCommandBuffer)cmd, invalid, 0, 2,
        (VkBuffer)(uintptr_t)1, UINT64_MAX, UINT64_MAX, VK_QUERY_RESULT_WAIT_BIT));
    cmd->recording_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    vk_ps4_CmdWriteTimestamp((VkCommandBuffer)cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, invalid, 0);
    assert(cmd->recording_error == VK_ERROR_OUT_OF_DEVICE_MEMORY);
    for (unsigned i = 0; i < 8; ++i) assert(words[i] == 0xdeadbeef);
    free(cmd);
    vk_ps4_DestroyQueryPool((VkDevice)&dev, VK_NULL_HANDLE, NULL);
    assert(releases == 0);
    VkPs4QueryPool *legacy = calloc(1, sizeof(*legacy));
    legacy->gnm_mem.allocated = true;
    vk_ps4_DestroyQueryPool((VkDevice)&dev, (VkQueryPool)legacy, NULL);
    assert(releases == 1);
    vk_ps4_DestroyInstance(instance, NULL);
    assert(frees == 3 && logs >= 10);
    puts("PASS: production ICD timestamp/occlusion gates, null outputs, unsupported reads/reset/copy, no GPU emission, cleanup");
}
