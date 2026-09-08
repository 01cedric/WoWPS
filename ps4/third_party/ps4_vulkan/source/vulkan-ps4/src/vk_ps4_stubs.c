/*
 * vk_ps4_stubs.c — Stub implementations for not-yet-implemented Vulkan functions.
 *
 * Phase 1 implements: memory, buffer, image, render pass, framebuffer, shader,
 * pipeline, command buffer, swapchain, queue, sync.
 *
 * Phase 2 implements: descriptors, compute pipelines, texture copy/blit/barriers.
 *
 * Phase 3 implements: GNM device lifecycle (init/teardown), EOP fence/semaphore
 * sync, query pools (occlusion + timestamp + copy results).
 *
 * Remaining stubs: some copy/blit commands (full 3D->2D blit, shader-based copy),
 * and the stub shader compile path (when libpsbc is unavailable).
 */

#include "vk_ps4_internal.h"

#include <string.h>

/* === Stub shader (when libpsbc is not available) === */
VkResult vk_ps4_shader_compile_stub(
    const uint32_t *spirv, size_t spirv_size,
    VkShaderStageFlagBits stage,
    void **out_binary, size_t *out_binary_size
) {
    (void)spirv; (void)spirv_size; (void)stage;
    if (!out_binary || !out_binary_size) return VK_ERROR_INITIALIZATION_FAILED;
    *out_binary = NULL;
    *out_binary_size = 0;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

/* === VK_KHR_surface stubs ===
 *
 * PS4 has no windowing-system surface: VideoOut is owned directly by the
 * swapchain (see vk_ps4_swapchain.c), and vkCreateSwapchainKHR accepts
 * VK_NULL_HANDLE for VkSwapchainCreateInfoKHR::surface. Nothing on PS4 calls
 * these - the only caller in the tree is imgui_impl_vulkan.cpp's desktop
 * ImGui_ImplVulkanH_CreateWindow* helpers, which WoWee's PS4 build never
 * calls - but that translation unit also contains functions we do use
 * (ImGui_ImplVulkan_Init and friends), so without these three symbols the
 * whole object stays unresolved at link time whenever section-level dead
 * code elimination (--gc-sections) is not enabled. */
VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities
) {
    (void)physicalDevice; (void)surface; (void)pSurfaceCapabilities;
    return VK_ERROR_SURFACE_LOST_KHR;
}

VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats
) {
    (void)physicalDevice; (void)surface; (void)pSurfaceFormats;
    if (pSurfaceFormatCount) *pSurfaceFormatCount = 0;
    return VK_ERROR_SURFACE_LOST_KHR;
}

VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes
) {
    (void)physicalDevice; (void)surface; (void)pPresentModes;
    if (pPresentModeCount) *pPresentModeCount = 0;
    return VK_ERROR_SURFACE_LOST_KHR;
}
