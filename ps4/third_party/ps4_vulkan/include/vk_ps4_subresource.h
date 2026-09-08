#ifndef VK_PS4_SUBRESOURCE_H
#define VK_PS4_SUBRESOURCE_H
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

static inline bool vk_ps4_resolve_range(uint32_t base, uint32_t count,
                                      uint32_t total, uint32_t *resolved) {
    if (!resolved || base >= total || count == 0) return false;
    *resolved = count == UINT32_MAX ? total - base : count;
    return *resolved <= total - base;
}
static inline bool vk_ps4_rect_in_surface(const VkRect2D *rect,
                                         uint32_t width, uint32_t height) {
    if (!rect || rect->offset.x < 0 || rect->offset.y < 0) return false;
    const uint32_t x = (uint32_t)rect->offset.x, y = (uint32_t)rect->offset.y;
    return x <= width && y <= height && rect->extent.width <= width - x &&
           rect->extent.height <= height - y;
}
#endif
