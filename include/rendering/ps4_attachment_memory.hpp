#pragma once
#include <vulkan/vulkan.h>

namespace wowee::rendering {
// GPU-written attachment images only. Sampled assets and CPU-visible buffers
// retain their existing policy. Do not use this fallback for shadow readback.
inline uint32_t ps4AttachmentMemoryTypeBits(
    VkImageUsageFlags usage, const VkPhysicalDeviceMemoryProperties& props) {
    if (!(usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))) return 0;
    uint32_t bits = 0;
    for (uint32_t i = 0; i < props.memoryTypeCount && i < 32; ++i) {
        const auto flags = props.memoryTypes[i].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            !(flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) bits |= 1u << i;
    }
    return bits;
}
struct Ps4AttachmentAllocationResult {
    VkResult result;
    VkResult preferredResult;
    bool fallback;
};
// The callback creates an image with the supplied VMA type mask. An empty
// preferred mask means no suitable type, not a restricted VMA allocation (VMA
// interprets zero as unrestricted). Retry only allocation/type exhaustion;
// device-loss/initialization errors must not be disguised by a second attempt.
template<class Allocate>
Ps4AttachmentAllocationResult allocatePs4Attachment(uint32_t preferred, Allocate allocate) {
    if (!preferred) return {allocate(0), VK_ERROR_FEATURE_NOT_PRESENT, true};
    const VkResult result = allocate(preferred);
    if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY || result == VK_ERROR_OUT_OF_HOST_MEMORY ||
        result == VK_ERROR_FEATURE_NOT_PRESENT)
        return {allocate(0), result, true};
    return {result, result, false};
}
}
