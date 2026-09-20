#pragma once
#include <vulkan/vulkan.h>

namespace wowee::rendering {
// PS4 Onion and Garlic both advertise DEVICE_LOCAL. Legacy GPU_ONLY therefore
// selects cached Onion first. The retired depth reader requires uncached Garlic.
// Restrict only the shadow atlas; do not change general VMA or heap policy.
inline uint32_t ps4ShadowMemoryTypeBits(const VkPhysicalDeviceMemoryProperties& props) {
    uint32_t bits = 0;
    for (uint32_t i = 0; i < props.memoryTypeCount && i < 32; ++i) {
        const auto flags = props.memoryTypes[i].propertyFlags;
        constexpr auto required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if ((flags & required) == required && !(flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
            bits |= 1u << i;
    }
    return bits;
}
}
