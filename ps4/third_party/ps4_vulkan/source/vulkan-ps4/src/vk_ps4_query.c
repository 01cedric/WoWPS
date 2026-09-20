/*
 * Query support is deliberately unavailable in this backend (G1 audit).
 *
 * The bundled EOP emitter encodes SEND_GPU_CLOCK but supplies no clock period
 * or guaranteed valid-bit/stage contract. Its Begin/EndQuery helpers do not
 * implement a readable ZPASS count: EndQuery emits an immediate zero write.
 * See docs/PS4_GPU_QUERY_CONTRACT.md for reproducible packet evidence.
 *
 * Fail before allocating or recording GPU work. In particular, a non-zero
 * payload is NOT an availability signal, and zero is a legitimate result.
 * Re-enabling queries requires completion separate from payload, ordered reset
 * and copy, proven pool lifetime, and calibrated units for Vulkan timestamps.
 */
#include "vk_ps4_internal.h"

static void unsupported_query_command(VkCommandBuffer commandBuffer,
                                      const char *operation) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (cmd->recording_error == VK_SUCCESS) {
        cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
        vk_ps4_log("%s: unsupported PS4 query operation; command buffer rejected", operation);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *info,
                       const VkAllocationCallbacks *allocator, VkQueryPool *pool) {
    (void)allocator;
    if (pool) *pool = VK_NULL_HANDLE;
    if (!device || !info || !pool ||
        info->sType != VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO || !info->queryCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (info->queryType == VK_QUERY_TYPE_TIMESTAMP)
        vk_ps4_log("CreateQueryPool: timestamp unsupported (uncalibrated-ps4; validBits=0 period=0)");
    else if (info->queryType == VK_QUERY_TYPE_OCCLUSION)
        vk_ps4_log("CreateQueryPool: occlusion unsupported (bundled GNM helper does not return ZPASS)");
    else
        vk_ps4_log("CreateQueryPool: query type %u unsupported", (unsigned)info->queryType);
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                        const VkAllocationCallbacks *allocator) {
    if (!device || !queryPool) return;
    /* Keep cleanup valid for a pool owned by the caller; creation above never
     * allocates one. As with Vulkan, the caller must first retire GPU users. */
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;
    if (pool->gnm_mem.allocated) sceGnmDirectMemoryRelease(&pool->gnm_mem);
    vk_ps4_free(allocator ? allocator : &dev->allocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                           uint32_t queryCount, size_t dataSize, void *data,
                           VkDeviceSize stride, VkQueryResultFlags flags) {
    (void)firstQuery; (void)queryCount; (void)dataSize; (void)stride; (void)flags;
    if (!device || !queryPool || !data) return VK_ERROR_INITIALIZATION_FAILED;
    /* Never wait on the host or copy stale payloads/fictional availability. */
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdResetQueryPool(VkCommandBuffer cmd, VkQueryPool pool,
                         uint32_t first, uint32_t count) {
    (void)pool; (void)first; (void)count;
    unsupported_query_command(cmd, "CmdResetQueryPool");
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBeginQuery(VkCommandBuffer cmd, VkQueryPool pool,
                     uint32_t query, VkQueryControlFlags flags) {
    (void)pool; (void)query; (void)flags;
    unsupported_query_command(cmd, "CmdBeginQuery");
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdEndQuery(VkCommandBuffer cmd, VkQueryPool pool, uint32_t query) {
    (void)pool; (void)query;
    unsupported_query_command(cmd, "CmdEndQuery");
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdWriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage,
                         VkQueryPool pool, uint32_t query) {
    (void)stage; (void)pool; (void)query;
    unsupported_query_command(cmd, "CmdWriteTimestamp");
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyQueryPoolResults(VkCommandBuffer cmd, VkQueryPool pool,
                              uint32_t first, uint32_t count, VkBuffer buffer,
                              VkDeviceSize offset, VkDeviceSize stride,
                              VkQueryResultFlags flags) {
    (void)pool; (void)first; (void)count; (void)buffer;
    (void)offset; (void)stride; (void)flags;
    unsupported_query_command(cmd, "CmdCopyQueryPoolResults");
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_ResetQueryPoolEXT(VkDevice device, VkQueryPool pool,
                        uint32_t first, uint32_t count) {
    (void)device; (void)pool; (void)first; (void)count;
    vk_ps4_log("ResetQueryPoolEXT: unsupported PS4 query operation; no memory modified");
}
