/*
 * vk_ps4_buffer.c — VkBuffer implementation via GnmBuffer.
 */

#include "vk_ps4_internal.h"

#include <string.h>

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
    if (!device || !pCreateInfo || !pBuffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Buffer *buf = vk_ps4_alloc_zero(alloc, sizeof(*buf), 16);
    if (!buf) return VK_ERROR_OUT_OF_HOST_MEMORY;
    buf->type = VK_PS4_OBJ_BUFFER;
    buf->device = dev;
    buf->create_info = *pCreateInfo;
    buf->memory = NULL;
    buf->memory_offset = 0;

    /* Initialize GnmBuffer descriptor to zero — will be filled on BindBufferMemory */
    memset(&buf->gnm_buffer, 0, sizeof(buf->gnm_buffer));

    *pBuffer = (VkBuffer)buf;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
    if (!device || !buffer) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, buf);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements) {
    (void)device;
    if (!buffer || !pMemoryRequirements) return;
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    /* GNM buffers need 4-byte alignment minimum, 256 for optimal GPU access */
    pMemoryRequirements->size = buf->create_info.size;
    pMemoryRequirements->alignment = 256;
    /* Buffers can live in either Onion or Garlic memory */
    pMemoryRequirements->memoryTypeBits = (1u << VK_PS4_MEMORY_TYPE_ONION) | (1u << VK_PS4_MEMORY_TYPE_GARLIC);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    if (!device || !buffer || !memory) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    VkPs4DeviceMemory *mem = (VkPs4DeviceMemory *)memory;

    /* Overflow-safe bounds check */
    if (offset > mem->size || buf->create_info.size > mem->size - offset) {
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    buf->memory = mem;
    buf->memory_offset = offset;

    /* Set up the GnmBuffer descriptor with the GPU address */
    if (!mem->gnm_mem.mapped) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    void *gpu_addr = (char *)mem->gnm_mem.mapped + offset;
    sceGnmBufSetBaseAddress(&buf->gnm_buffer, gpu_addr);

    return VK_SUCCESS;
}

/* === Buffer View === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkBufferView *pBufferView) {
    if (!device || !pCreateInfo || !pBufferView) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Buffer *buf = (VkPs4Buffer *)pCreateInfo->buffer;
    if (!buf || !buf->memory || !buf->memory->gnm_mem.mapped) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Validate offset/range against buffer size (overflow-safe) */
    if (pCreateInfo->offset > buf->create_info.size) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    uint64_t range = pCreateInfo->range;
    if (range == VK_WHOLE_SIZE) {
        range = buf->create_info.size - pCreateInfo->offset;
    }
    /* Overflow-safe: range > remaining is safe because offset <= size */
    if (range > buf->create_info.size - pCreateInfo->offset) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Convert VkFormat to a BUF-compatible GnmDataFormat.
     * SRGB and BC compressed formats have no buffer representation. */
    GnmDataFormat fmt = vk_ps4_vk_format_to_gnm_buffer(pCreateInfo->format);
    if (fmt.asuint == 0) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    VkPs4BufferView *view = vk_ps4_alloc_zero(alloc, sizeof(*view), 16);
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    view->type = VK_PS4_OBJ_BUFFER_VIEW;
    view->device = dev;
    view->buffer = buf;
    view->format = pCreateInfo->format;
    view->offset = pCreateInfo->offset;
    view->range = pCreateInfo->range;

    /* Build the V# (GnmBuffer) descriptor for texel buffer access.
     * The base address is the buffer's GPU address + view offset. */
    void *base_addr = (char *)buf->memory->gnm_mem.mapped +
                      buf->memory_offset + pCreateInfo->offset;
    sceGnmBufSetBaseAddress(&view->gnm_buffer, base_addr);

    /* Set the BUF-compatible format */
    sceGnmBufSetFormat(&view->gnm_buffer, fmt);

    /* Determine the element size and total range */
    uint32_t element_size = sceGnmDfGetBytesPerElement(fmt);
    if (element_size == 0) element_size = 4;

    /* Validate offset alignment to element size */
    if (pCreateInfo->offset % element_size != 0) {
        vk_ps4_free(alloc, view);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Stride = element size (texel buffer: one texel per record) */
    view->gnm_buffer.stride = element_size;
    view->gnm_buffer.numrecords = (uint32_t)(range / element_size);

    /* Read-only memory type for uniform texel buffers */
    sceGnmBufSetMemoryType(&view->gnm_buffer, GNM_MEMORY_READONLY, false, false);

    *pBufferView = (VkBufferView)view;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks *pAllocator) {
    if (!device || !bufferView) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4BufferView *view = (VkPs4BufferView *)bufferView;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, view);
}

/* === VK_KHR_buffer_device_address === */

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
vk_ps4_GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo) {
    (void)device;
    if (!pInfo || !pInfo->buffer) return 0;
    VkPs4Buffer *buf = (VkPs4Buffer *)pInfo->buffer;
    if (!buf->memory) return 0;
    /* GNM direct memory: directmemory is the GPU-visible address. */
    return (VkDeviceAddress)(buf->memory->gnm_mem.directmemory + buf->memory_offset);
}
