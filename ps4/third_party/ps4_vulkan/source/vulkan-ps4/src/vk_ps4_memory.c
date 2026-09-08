/*
 * vk_ps4_memory.c — VkDeviceMemory implementation via GNM direct memory.
 *
 * Maps VkAllocateMemory → sceGnmDirectMemoryAllocate,
 * vkMapMemory → use pre-mapped GnmDirectMemory.mapped pointer,
 * vkFreeMemory → sceGnmDirectMemoryRelease.
 */

#include "vk_ps4_internal.h"

#include <string.h>

static void vk_ps4_memory_sfence(void) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    /* Publish CPU stores to mapped Onion/Garlic memory before a following
     * GNM DMA or shader fetch.  This is essential for WC Garlic mappings and
     * harmless for coherent Onion mappings. */
    __asm__ volatile("sfence" ::: "memory");
#endif
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_AllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo *pAllocateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDeviceMemory *pMemory
) {
    if (!device || !pAllocateInfo || !pMemory) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pAllocateInfo->sType != VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4DeviceMemory *mem = vk_ps4_alloc_zero(alloc, sizeof(*mem), 16);
    if (!mem) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    mem->type = VK_PS4_OBJ_DEVICE_MEMORY;
    mem->device = dev;
    mem->size = pAllocateInfo->allocationSize;
    mem->memory_type_index = pAllocateInfo->memoryTypeIndex;
    mem->mapped_ptr = NULL;
    mem->mapped_offset = 0;
    mem->mapped_size = 0;

    /* Map Vulkan memory type index to GNM memory type */
    int32_t gnm_memory_type;
    int32_t gnm_protection = GNM_PROT_CPU_GPU_RW;

    switch (pAllocateInfo->memoryTypeIndex) {
    case VK_PS4_MEMORY_TYPE_ONION:
        /* Onion: CPU-coherent memory — use type 0 (cached) */
        gnm_memory_type = 0;  /* WB_ONION */
        break;
    case VK_PS4_MEMORY_TYPE_GARLIC:
        /* Garlic: GPU-local write-combined */
        gnm_memory_type = GNM_DIRECT_MEMORY_TYPE_WC_GARLIC;
        break;
    default:
        vk_ps4_free(alloc, mem);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    /* Alignment: 64KB for GPU memory (GNM direct memory requirement) */
    uint64_t alignment = 64 * 1024;

    GnmError err = sceGnmDirectMemoryAllocate(
        &mem->gnm_mem, mem->size, alignment, gnm_memory_type, gnm_protection
    );
    if (err != GNM_ERROR_OK) {
        vk_ps4_free(alloc, mem);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    *pMemory = (VkDeviceMemory)mem;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator) {
    if (!device || !memory) {
        return;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4DeviceMemory *mem = (VkPs4DeviceMemory *)memory;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    if (mem->gnm_mem.allocated) {
        sceGnmDirectMemoryRelease(&mem->gnm_mem);
    }
    vk_ps4_free(alloc, mem);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_MapMemory(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkMemoryMapFlags flags,
    void **ppData
) {
    (void)device;
    (void)flags;

    if (!memory || !ppData) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4DeviceMemory *mem = (VkPs4DeviceMemory *)memory;

    if (!mem->gnm_mem.mapped) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    /* Bounds check with overflow safety */
    if (offset > mem->size) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size == VK_WHOLE_SIZE) {
        size = mem->size - offset;
    } else if (size > mem->size - offset) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    mem->mapped_offset = offset;
    mem->mapped_size = size;
    mem->mapped_ptr = (char *)mem->gnm_mem.mapped + offset;
    *ppData = mem->mapped_ptr;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_UnmapMemory(VkDevice device, VkDeviceMemory memory) {
    (void)device;
    if (!memory) {
        return;
    }
    VkPs4DeviceMemory *mem = (VkPs4DeviceMemory *)memory;
    vk_ps4_memory_sfence();
    mem->mapped_ptr = NULL;
    mem->mapped_offset = 0;
    mem->mapped_size = 0;
    /* GNM direct memory stays mapped — nothing to unmap */
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_FlushMappedMemoryRanges(VkDevice device, uint32_t rangeCount, const VkMappedMemoryRange *pRanges) {
    /* A compiler barrier is insufficient for write-combined Garlic. */
    vk_ps4_memory_sfence();
    (void)device;
    (void)rangeCount;
    (void)pRanges;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_InvalidateMappedMemoryRanges(VkDevice device, uint32_t rangeCount, const VkMappedMemoryRange *pRanges) {
    /* GNM direct memory is directly visible to GPU — invalidate is a no-op. */
    (void)device;
    (void)rangeCount;
    (void)pRanges;
    return VK_SUCCESS;
}
