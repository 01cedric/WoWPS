/*
 * vk_ps4_query.c — VkQueryPool implementation via GNM occlusion queries.
 *
 * Query pools allocate a GPU-visible buffer to store query results.
 * Occlusion queries use the DB ZPASS counter via GNM BeginQuery/EndQuery.
 * Timestamp queries use EOP event writes with the GPU timestamp.
 */

#include "vk_ps4_internal.h"

#include <string.h>

/* Each query result is a uint64_t.
 * With VK_QUERY_RESULT_64_BIT: 8 bytes per query.
 * Without (32-bit): 4 bytes per query (lower 32 bits).
 * With VK_QUERY_RESULT_WAIT_BIT: poll until result is non-zero.
 * With VK_QUERY_RESULT_WITH_AVAILABILITY_BIT: extra uint64 for availability. */

#define QUERY_SLOT_SIZE 8  /* uint64_t per query result */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool) {
    if (!device || !pCreateInfo || !pQueryPool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->sType != VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Only occlusion and timestamp queries are supported */
    if (pCreateInfo->queryType != VK_QUERY_TYPE_OCCLUSION &&
        pCreateInfo->queryType != VK_QUERY_TYPE_TIMESTAMP) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4QueryPool *pool = vk_ps4_alloc_zero(alloc, sizeof(*pool), 16);
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = VK_PS4_OBJ_QUERY_POOL;
    pool->device = dev;
    pool->create_info = *pCreateInfo;

    /* Allocate GPU-visible memory for query results.
     * Each query needs QUERY_SLOT_SIZE bytes.
     * Use Onion memory (CPU-visible) for easy readback. */
    pool->result_size = (VkDeviceSize)pCreateInfo->queryCount * QUERY_SLOT_SIZE;
    pool->result_buffer = NULL;
    pool->result_gpu_addr = 0;

    /* Allocate direct memory for the query results */
    GnmDirectMemory query_mem;
    memset(&query_mem, 0, sizeof(query_mem));
    uint64_t alignment = 64 * 1024;  /* 64KB alignment for GNM */
    GnmError err = sceGnmDirectMemoryAllocate(
        &query_mem, pool->result_size, alignment, 0, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK) {
        vk_ps4_free(alloc, pool);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    /* Store the full direct memory handle for proper release */
    pool->gnm_mem = query_mem;
    pool->result_buffer = query_mem.mapped;
    pool->result_gpu_addr = (uint64_t)query_mem.mapped;

    /* Zero the result buffer */
    if (pool->result_buffer) {
        memset(pool->result_buffer, 0, pool->result_size);
    }

    *pQueryPool = (VkQueryPool)pool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator) {
    if (!device || !queryPool) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* Release the direct memory using the stored full handle.
     * Check the 'allocated' flag rather than result_buffer to avoid
     * leaking if result_buffer is NULL but gnm_mem was allocated. */
    if (pool->gnm_mem.allocated) {
        sceGnmDirectMemoryRelease(&pool->gnm_mem);
    }

    vk_ps4_free(alloc, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                           uint32_t queryCount, size_t dataSize, void *pData,
                           VkDeviceSize stride, VkQueryResultFlags flags) {
    (void)device;
    if (!queryPool || !pData) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;

    if (!pool->result_buffer) return VK_ERROR_INITIALIZATION_FAILED;
    /* Overflow-safe bounds check */
    if (firstQuery > pool->create_info.queryCount ||
        queryCount > pool->create_info.queryCount - firstQuery) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    bool wait = (flags & VK_QUERY_RESULT_WAIT_BIT) != 0;
    bool with_availability = (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != 0;
    bool result_64 = (flags & VK_QUERY_RESULT_64_BIT) != 0;

    /* Validate dataSize is large enough for the requested results.
     * Each query needs (result_64 ? 8 : 4) bytes, plus the same for
     * availability if requested. The last query may not need the full
     * stride, but we use stride for all but the last for safety. */
    size_t per_query = (result_64 ? 8 : 4) + (with_availability ? (result_64 ? 8 : 4) : 0);
    if (queryCount > 0 && dataSize < (size_t)(queryCount - 1) * stride + per_query) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    uint8_t *dst = (uint8_t *)pData;
    uint64_t *src = (uint64_t *)pool->result_buffer;

    for (uint32_t i = 0; i < queryCount; i++) {
        uint32_t query_idx = firstQuery + i;
        uint64_t *query_slot = &src[query_idx];

        /* For both occlusion and timestamp queries, a non-zero result
         * means the query has been written by the GPU. */
        uint64_t result = *query_slot;
        bool available = (result != 0);

        /* If WAIT_BIT is set, spin until the result is available.
         * KNOWN LIMITATION: This is a CPU busy-wait. On real PS4, we'd
         * need to use a fence or EOP wait to avoid spinning. */
        if (wait && !available) {
            /* For now, just read the current value — on the host stub,
             * queries are never actually written, so we'd spin forever.
             * Return VK_NOT_READY instead. */
            return VK_NOT_READY;
        }

        /* Write result to output buffer */
        if (result_64) {
            *(uint64_t *)dst = result;
            dst += 8;
            if (with_availability) {
                *(uint64_t *)dst = available ? 1 : 0;
                dst += 8;
            }
        } else {
            *(uint32_t *)dst = (uint32_t)result;
            dst += 4;
            if (with_availability) {
                *(uint32_t *)dst = available ? 1 : 0;
                dst += 4;
            }
        }

        if (stride > 0) {
            /* Advance to next slot based on stride */
            dst = (uint8_t *)pData + (i + 1) * stride;
        }
    }

    /* If not all results were available and WAIT wasn't set, return VK_NOT_READY */
    if (!wait) {
        for (uint32_t i = 0; i < queryCount; i++) {
            if (src[firstQuery + i] == 0) {
                return VK_NOT_READY;
            }
        }
    }

    return VK_SUCCESS;
}

/* === Command buffer query operations === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                         uint32_t firstQuery, uint32_t queryCount) {
    if (!commandBuffer || !queryPool) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;

    /* Reset each query slot by writing 0 to its GPU address */
    for (uint32_t i = 0; i < queryCount; i++) {
        uint64_t addr = pool->result_gpu_addr + (firstQuery + i) * QUERY_SLOT_SIZE;
        sceGnmDrawCmdResetQuery(&cmd->gnm_cmd, addr);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                     uint32_t query, VkQueryControlFlags flags) {
    if (!commandBuffer || !queryPool) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;

    if (query >= pool->create_info.queryCount) return;

    uint64_t addr = pool->result_gpu_addr + query * QUERY_SLOT_SIZE;
    /* Reset the query slot to 0 before beginning */
    sceGnmDrawCmdResetQuery(&cmd->gnm_cmd, addr);
    /* Enable the occlusion counter */
    sceGnmDrawCmdBeginQuery(&cmd->gnm_cmd, addr);

    (void)flags;  /* VK_QUERY_CONTROL_PRECISE_BIT not supported yet */
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) {
    if (!commandBuffer || !queryPool) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;

    if (query >= pool->create_info.queryCount) return;

    uint64_t addr = pool->result_gpu_addr + query * QUERY_SLOT_SIZE;
    /* Write the ZPASS count to the query slot and disable the counter */
    sceGnmDrawCmdEndQuery(&cmd->gnm_cmd, addr);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
                         VkQueryPool queryPool, uint32_t query) {
    if (!commandBuffer || !queryPool) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;

    if (query >= pool->create_info.queryCount) return;

    uint64_t addr = pool->result_gpu_addr + query * QUERY_SLOT_SIZE;

    /* Write the GPU timestamp via EOP event.
     * GNM_DATA_SEL_SEND_GPU_CLOCK writes the 32-bit GPU clock counter
     * to the specified GPU address. The upper 4 bytes of the 8-byte
     * query slot remain zero. When read as uint64_t, the timestamp
     * is only 32-bit — sufficient for relative timing. A full 64-bit
     * timestamp would require two EOP writes or a different data selector. */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_BOTTOM_OF_PIPE_TS, addr,
        GNM_DATA_SEL_SEND_GPU_CLOCK, 0);

    (void)pipelineStage;  /* All stages map to the same EOP event */
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                uint32_t firstQuery, uint32_t queryCount,
                                VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                VkDeviceSize stride, VkQueryResultFlags flags) {
    if (!commandBuffer || !queryPool || !dstBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;

    if (!dst || !dst->memory || !dst->memory->gnm_mem.mapped) return;
    /* Overflow-safe bounds check */
    if (firstQuery > pool->create_info.queryCount ||
        queryCount > pool->create_info.queryCount - firstQuery) return;

    /* Copy query results from the query pool's GPU memory to the destination buffer.
     * We use sceGnmDrawCmdCopyMemory for each query result.
     * KNOWN LIMITATION: This copies the raw uint64 results without
     * handling VK_QUERY_RESULT_WITH_AVAILABILITY_BIT or 32-bit conversion.
     * The stride is used to place each result at the correct offset. */
    bool result_64 = (flags & VK_QUERY_RESULT_64_BIT) != 0;
    uint32_t copy_size = result_64 ? 8 : 4;

    if (stride == 0) stride = copy_size;

    uint64_t dst_base = (uint64_t)((char *)dst->memory->gnm_mem.mapped +
                                    dst->memory_offset + dstOffset);

    for (uint32_t i = 0; i < queryCount; i++) {
        uint64_t src_addr = pool->result_gpu_addr + (firstQuery + i) * QUERY_SLOT_SIZE;
        uint64_t dst_addr = dst_base + i * stride;
        sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, dst_addr, src_addr, copy_size);
    }
}

/* === VK_EXT_host_query_reset === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_ResetQueryPoolEXT(VkDevice device, VkQueryPool queryPool,
                         uint32_t firstQuery, uint32_t queryCount) {
    (void)device;
    if (!queryPool) return;
    VkPs4QueryPool *pool = (VkPs4QueryPool *)queryPool;
    /* Reset query results on the host by zeroing the result memory. */
    if (pool->result_gpu_addr && pool->create_info.queryCount > 0) {
        uint32_t end = firstQuery + queryCount;
        if (end > pool->create_info.queryCount) end = pool->create_info.queryCount;
        for (uint32_t i = firstQuery; i < end; i++) {
            volatile uint64_t *slot =
                (volatile uint64_t *)((char *)pool->result_buffer + i * QUERY_SLOT_SIZE);
            *slot = 0;
        }
    }
}
