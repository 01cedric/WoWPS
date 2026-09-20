#ifndef VK_PS4_H
#define VK_PS4_H

/*
 * vulkan-ps4 — public header.
 *
 * This ICD implements Vulkan 1.0 over OpenGNM (PS4 GNM graphics API).
 * Applications link against libvulkan_ps4 and use standard Vulkan calls.
 *
 * On PS4, there is no standard Vulkan loader. Applications link
 * libvulkan_ps4 statically or load it via a custom path. The ICD
 * exports all vk* symbols directly.
 *
 * See VULKAN_PS4_PLAN.md for the full architecture and status.
 */

#include <stdint.h>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ICD loader interface — used by the Vulkan loader on host for testing.
 * On PS4, applications call vk* symbols directly. */
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pVersion);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR VkResult VKAPI_CALL vk_icdEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);
VKAPI_ATTR VkResult VKAPI_CALL vk_icdEnumerateInstanceLayerProperties(uint32_t *pPropertyCount, VkLayerProperties *pProperties);

/*
 * Read-only description of one already-acquired VideoOut buffer.
 *
 * `pixels` points at VideoOut-owned direct memory.  It is intentionally not
 * Vulkan-mappable memory and must never be freed or retained after the image
 * is presented.  The caller must first acquire `imageIndex`, then establish a
 * display-safe write boundary (for example an M65 submitted-and-waited
 * WaitUntilSafeForRendering pass) before writing pixels.  This query does not
 * acquire an image, submit GPU work, or present it.
 */
typedef struct VkPs4ScanoutInfo {
    void *pixels;
    VkDeviceSize byteSize;
    uint32_t width;
    uint32_t height;
    uint32_t rowPitchBytes;
    uint32_t bytesPerPixel;
    VkFormat vkFormat;
    /* Native sceVideoOut registration format; retain it for pixel-packing
     * diagnostics instead of assuming the requested Vulkan format's byte
     * ordering. */
    uint32_t nativeVideoOutFormat;
    uint32_t imageIndex;
    VkBool32 acquired;
} VkPs4ScanoutInfo;

/*
 * Exposes validated metadata for a currently acquired scanout image.  This is
 * a PS4 ICD bridge, not a standard Vulkan entry point.  It deliberately lives
 * outside the VkSwapchainKHR opaque handle so applications never cast that
 * handle to internal VkPs4Swapchain storage.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkPs4GetAcquiredScanoutInfo(
    VkSwapchainKHR swapchain, uint32_t imageIndex,
    VkPs4ScanoutInfo *pInfo
);

/*
 * Diagnostic bridge (black-screen investigation): exposes a swapchain
 * image's own backing VkDeviceMemory handle — the same VkPs4DeviceMemory
 * that wraps VideoOut's own display buffer — so it can be bound to a
 * VK_BUFFER_USAGE_STORAGE_BUFFER_BIT buffer via vkBindBufferMemory. The
 * standard graphics-pipeline color-buffer export has been proven, across
 * every tile mode/memory-source/draw-type combination tested, to never
 * produce visible output on this hardware/firmware; this lets a compute
 * shader (a different GPU write path — buffer stores, not CB export)
 * write directly into the same memory instead, to determine whether that
 * path actually reaches the display.
 *
 * The returned handle aliases live swapchain memory — never call
 * vkFreeMemory on it, and only write to it under the same display-safe
 * timing rules as vkPs4GetAcquiredScanoutInfo (image acquired, not
 * currently owned by the display engine).
 */
VKAPI_ATTR VkResult VKAPI_CALL vkPs4GetSwapchainImageMemory(
    VkSwapchainKHR swapchain, uint32_t imageIndex,
    VkDeviceMemory *pMemory
);

/* Report the actual physical storage class (VideoOut is linear even when
 * its Vulkan swapchain creation metadata says OPTIMAL). */
VKAPI_ATTR VkBool32 VKAPI_CALL vkPs4ImageHasLinearStorage(VkImage image);

/* Bounded diagnostic read of a retired, uncompressed D32 Garlic image.
 * Caller MUST have waited for the image's last GPU submission to complete,
 * including depth-cache release, and must prevent concurrent GPU writes.
 * This function does not wait or submit work. At most 32x32 cell centres are
 * sampled within region. Counts describe samples, never full pixel coverage.
 * finiteCount includes finite out-of-range values; invalidCount includes both
 * non-finite and out-of-[0,1] values. min/max cover finite values, or are zero
 * when none exist. nonClearCount counts valid depths strictly below 1. */
/* Rejection bitmask; zero means no guard rejected the read. */
typedef enum VkPs4DepthInspectionReject {
    VK_PS4_DEPTH_REJECT_ARGUMENT = 1u << 0,
    VK_PS4_DEPTH_REJECT_DEVICE = 1u << 1,
    VK_PS4_DEPTH_REJECT_FORMAT = 1u << 2,
    VK_PS4_DEPTH_REJECT_SHAPE = 1u << 3,
    VK_PS4_DEPTH_REJECT_SAMPLES = 1u << 4,
    VK_PS4_DEPTH_REJECT_HTILE = 1u << 5,
    VK_PS4_DEPTH_REJECT_MEMORY = 1u << 6,
    VK_PS4_DEPTH_REJECT_MAPPING = 1u << 7,
    VK_PS4_DEPTH_REJECT_MEMORY_TYPE = 1u << 8,
    VK_PS4_DEPTH_REJECT_REGION = 1u << 9,
    VK_PS4_DEPTH_REJECT_LAYOUT = 1u << 10,
    VK_PS4_DEPTH_REJECT_COORD = 1u << 11
} VkPs4DepthInspectionReject;
typedef struct VkPs4DepthInspection {
    uint32_t sampleCount;
    uint32_t finiteCount;
    uint32_t nonClearCount;
    uint32_t invalidCount;
    float minDepth;
    float maxDepth;
    uint32_t rejectionReason;
    uint32_t memoryTypeIndex; /* UINT32_MAX when no bound allocation is known. */
} VkPs4DepthInspection;
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_InspectRetiredDepthImage(
    VkDevice device, VkImage image, const VkRect2D *region,
    VkPs4DepthInspection *result);

#ifdef __cplusplus
}
#endif

#endif /* VK_PS4_H */
