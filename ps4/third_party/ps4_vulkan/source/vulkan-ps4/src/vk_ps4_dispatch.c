/*
 * vk_ps4_dispatch.c — Vulkan function dispatch table.
 *
 * Maps Vulkan function names to implementation pointers.
 * vkGetInstanceProcAddr and vkGetDeviceProcAddr look up names here.
 *
 * Functions not yet implemented return VK_ERROR_FEATURE_NOT_PRESENT or are
 * stubbed. As phases progress, stubs are replaced with real implementations.
 */

#include "vk_ps4.h"
#include "vk_ps4_internal.h"
#include "vk_ps4_cache_abi.h"

#include <string.h>

/* === Forward declarations of all implemented functions === */

/* Instance */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateInstance(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyInstance(VkInstance, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EnumeratePhysicalDevices(VkInstance, uint32_t *, VkPhysicalDevice *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties *);

/* Device */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *, VkDevice *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDevice(VkDevice, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue *);

/* Memory */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateMemory(VkDevice, const VkMemoryAllocateInfo *, const VkAllocationCallbacks *, VkDeviceMemory *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_FreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_MapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void **);
VKAPI_ATTR void VKAPI_CALL vk_ps4_UnmapMemory(VkDevice, VkDeviceMemory);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_FlushMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_InvalidateMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange *);

/* Buffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateBuffer(VkDevice, const VkBufferCreateInfo *, const VkAllocationCallbacks *, VkBuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyBuffer(VkDevice, VkBuffer, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);

/* Image */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImage(VkDevice, const VkImageCreateInfo *, const VkAllocationCallbacks *, VkImage *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImage(VkDevice, VkImage, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImageView(VkDevice, const VkImageViewCreateInfo *, const VkAllocationCallbacks *, VkImageView *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks *);

/* Render pass / framebuffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateRenderPass(VkDevice, const VkRenderPassCreateInfo *, const VkAllocationCallbacks *, VkRenderPass *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyRenderPass(VkDevice, VkRenderPass, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateFramebuffer(VkDevice, const VkFramebufferCreateInfo *, const VkAllocationCallbacks *, VkFramebuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks *);

/* Shader / pipeline */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateShaderModule(VkDevice, const VkShaderModuleCreateInfo *, const VkAllocationCallbacks *, VkShaderModule *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyShaderModule(VkDevice, VkShaderModule, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo *, const VkAllocationCallbacks *, VkPipelineLayout *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateGraphicsPipelines(VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo *, const VkAllocationCallbacks *, VkPipeline *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateComputePipelines(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo *, const VkAllocationCallbacks *, VkPipeline *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks *);

/* Descriptor */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo *, const VkAllocationCallbacks *, VkDescriptorSetLayout *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo *, const VkAllocationCallbacks *, VkDescriptorPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo *, VkDescriptorSet *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_FreeDescriptorSets(VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_UpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet *, uint32_t, const VkCopyDescriptorSet *);

/* Command buffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateCommandPool(VkDevice, const VkCommandPoolCreateInfo *, const VkAllocationCallbacks *, VkCommandPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyCommandPool(VkDevice, VkCommandPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateCommandBuffers(VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_FreeCommandBuffers(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BeginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EndCommandBuffer(VkCommandBuffer);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetCommandBuffer(VkCommandBuffer, VkCommandBufferResetFlags);

/* Command buffer recording */
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetViewport(VkCommandBuffer, uint32_t, uint32_t, const VkViewport *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetScissor(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindVertexBuffers(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer *, const VkDeviceSize *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDraw(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexed(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexedIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBlitImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageBlit *, VkFilter);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBufferToImage(VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImageToBuffer(VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo *, VkSubpassContents);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdNextSubpass(VkCommandBuffer, VkSubpassContents);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndRenderPass(VkCommandBuffer);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginRenderPass2(VkCommandBuffer, const VkRenderPassBeginInfo *, const VkSubpassBeginInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdNextSubpass2(VkCommandBuffer, const VkSubpassBeginInfo *, const VkSubpassEndInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndRenderPass2(VkCommandBuffer, const VkSubpassEndInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearColorImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue *, uint32_t, const VkImageSubresourceRange *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearDepthStencilImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue *, uint32_t, const VkImageSubresourceRange *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearAttachments(VkCommandBuffer, uint32_t, const VkClearAttachment *, uint32_t, const VkClearRect *);

/* Queue */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueueSubmit(VkQueue, uint32_t, const VkSubmitInfo *, VkFence);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueueWaitIdle(VkQueue);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_DeviceWaitIdle(VkDevice);

/* Sync */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateFence(VkDevice, const VkFenceCreateInfo *, const VkAllocationCallbacks *, VkFence *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyFence(VkDevice, VkFence, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_WaitForFences(VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetFences(VkDevice, uint32_t, const VkFence *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetFenceStatus(VkDevice, VkFence);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSemaphore(VkDevice, const VkSemaphoreCreateInfo *, const VkAllocationCallbacks *, VkSemaphore *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySemaphore(VkDevice, VkSemaphore, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateEvent(VkDevice, const VkEventCreateInfo *, const VkAllocationCallbacks *, VkEvent *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyEvent(VkDevice, VkEvent, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetEventStatus(VkDevice, VkEvent);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_SetEvent(VkDevice, VkEvent);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetEvent(VkDevice, VkEvent);

/* Query */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateQueryPool(VkDevice, const VkQueryPoolCreateInfo *, const VkAllocationCallbacks *, VkQueryPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyQueryPool(VkDevice, VkQueryPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void *, VkDeviceSize, VkQueryResultFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdWriteTimestamp(VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t);

/* Swapchain (WSI) */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t *, VkImage *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueuePresentKHR(VkQueue, const VkPresentInfoKHR *);

/* === Not-yet-implemented stubs === */
#define STUB0(name, rettype, retval) \
    VKAPI_ATTR rettype VKAPI_CALL name(void) { return retval; }
#define STUB_NOT_IMPL(name, rettype, retval) \
    VKAPI_ATTR rettype VKAPI_CALL name(void) { return retval; }

/* Functions we haven't implemented yet — return VK_ERROR_FEATURE_NOT_PRESENT */

/* Sparse image format properties: no sparse support — report 0 properties. */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                                     VkImageType type, VkSampleCountFlagBits samples,
                                                     VkImageUsageFlags usage, VkImageTiling tiling,
                                                     uint32_t *pPropertyCount,
                                                     VkSparseImageFormatProperties *pProperties) {
    (void)physicalDevice; (void)format; (void)type; (void)samples;
    (void)usage; (void)tiling; (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                               VkImageType type, VkImageTiling tiling,
                                               VkImageUsageFlags usage, VkImageCreateFlags flags,
                                               VkImageFormatProperties *pImageFormatProperties) {
    (void)physicalDevice; (void)flags;
    if (!pImageFormatProperties) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    /* Report basic properties based on format.
     * Max dimensions are conservative for PS4 (GCN). */
    memset(pImageFormatProperties, 0, sizeof(*pImageFormatProperties));
    pImageFormatProperties->maxExtent.width = 16384;
    pImageFormatProperties->maxExtent.height = 16384;
    pImageFormatProperties->maxExtent.depth = 2048;
    pImageFormatProperties->maxMipLevels = 15;
    pImageFormatProperties->maxArrayLayers = 2048;
    pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    pImageFormatProperties->maxResourceSize = 0xFFFFFFFF;
    (void)format; (void)type; (void)tiling; (void)usage;
    return VK_SUCCESS;
}

/* Memory requirements 2 — thin wrapper over the v1 functions. */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetBufferMemoryRequirements2(VkDevice device,
                                     const VkBufferMemoryRequirementsInfo2 *pInfo,
                                     VkMemoryRequirements2 *pMemoryRequirements) {
    if (!pInfo || !pMemoryRequirements) return;
    vk_ps4_GetBufferMemoryRequirements(device, pInfo->buffer,
        &pMemoryRequirements->memoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetImageMemoryRequirements2(VkDevice device,
                                    const VkImageMemoryRequirementsInfo2 *pInfo,
                                    VkMemoryRequirements2 *pMemoryRequirements) {
    if (!pInfo || !pMemoryRequirements) return;
    vk_ps4_GetImageMemoryRequirements(device, pInfo->image,
        &pMemoryRequirements->memoryRequirements);
}

/* Render area granularity — return {1,1} (no granularity constraint). */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass,
                                 VkExtent2D *pGranularity) {
    (void)device; (void)renderPass;
    if (pGranularity) { pGranularity->width = 1; pGranularity->height = 1; }
}

/* Device memory commitment — always 0 (no sparse memory). */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                                  VkDeviceSize *pCommittedMemoryInBytes) {
    (void)device; (void)memory;
    if (pCommittedMemoryInBytes) *pCommittedMemoryInBytes = 0;
}

/* Sparse image memory requirements — no sparse support. */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetImageSparseMemoryRequirements(VkPhysicalDevice physicalDevice, VkImage image,
                                         uint32_t *pSparseMemoryRequirementCount,
                                         VkSparseImageMemoryRequirements *pSparseMemoryRequirements) {
    (void)physicalDevice; (void)image; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo,
                                          uint32_t *pSparseMemoryRequirementCount,
                                          VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements) {
    (void)device; (void)pInfo; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

/* Queue family properties 2 — thin wrapper over v1. */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice,
                                                uint32_t *pQueueFamilyPropertyCount,
                                                VkQueueFamilyProperties2 *pQueueFamilyProperties) {
    if (!pQueueFamilyProperties) {
        vk_ps4_GetPhysicalDeviceQueueFamilyProperties(physicalDevice,
            pQueueFamilyPropertyCount, NULL);
    } else {
        /* Extract the inner VkQueueFamilyProperties pointers from each
         * VkQueueFamilyProperties2 struct so we can fill them in a batch. */
        uint32_t count = *pQueueFamilyPropertyCount;
        VkQueueFamilyProperties props[VK_PS4_NUM_QUEUE_FAMILIES];
        for (uint32_t i = 0; i < count && i < VK_PS4_NUM_QUEUE_FAMILIES; i++) {
            props[i] = pQueueFamilyProperties[i].queueFamilyProperties;
        }
        vk_ps4_GetPhysicalDeviceQueueFamilyProperties(physicalDevice,
            &count, props);
        for (uint32_t i = 0; i < count && i < VK_PS4_NUM_QUEUE_FAMILIES; i++) {
            pQueueFamilyProperties[i].queueFamilyProperties = props[i];
        }
        *pQueueFamilyPropertyCount = count;
    }
}

/* Pipeline cache — stores compiled shader binaries for reuse.
 * See VkPs4PipelineCache in vk_ps4_internal.h for the data format. */

/* FNV-1a 64-bit hash — fast, good distribution for SPIR-V code */
uint64_t vk_ps4_pipeline_cache_hash(const void *spirv, size_t spirv_size,
                                     uint32_t stage) {
    const uint8_t *data = (const uint8_t *)spirv;
    uint64_t hash = 1469598103934665603ULL;  /* FNV offset basis */
    hash ^= (uint64_t)stage;
    hash *= 1099511628211ULL;  /* FNV prime */
    for (size_t i = 0; i < spirv_size; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void *vk_ps4_pipeline_cache_lookup(VkPipelineCache cache, uint64_t hash,
                                    uint32_t stage, size_t *out_size) {
    if (!cache) return NULL;
    VkPs4PipelineCache *pc = (VkPs4PipelineCache *)cache;
    for (uint32_t i = 0; i < pc->entry_count; i++) {
        if (pc->entries[i].hash == hash && pc->entries[i].stage == stage) {
            if (out_size) *out_size = pc->entries[i].binary_size;
            return pc->entries[i].binary;
        }
    }
    return NULL;
}

VkResult vk_ps4_pipeline_cache_insert(VkPipelineCache cache, uint64_t hash,
                                       uint32_t stage, uint32_t spirv_size,
                                       const void *binary, size_t binary_size) {
    if (!cache) return VK_SUCCESS;
    if (!binary || !binary_size || binary_size > UINT32_MAX)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4PipelineCache *pc = (VkPs4PipelineCache *)cache;
    if (pc->entry_count >= VK_PS4_PIPELINE_CACHE_MAX_ENTRIES) return VK_SUCCESS;
    /* Check for duplicate */
    for (uint32_t i = 0; i < pc->entry_count; i++) {
        if (pc->entries[i].hash == hash && pc->entries[i].stage == stage)
            return VK_SUCCESS;
    }
    /* Insert — copy the binary so the caller can free their copy */
    void *binary_copy = vk_ps4_alloc(&pc->allocator, binary_size, 16);
    if (!binary_copy) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memcpy(binary_copy, binary, binary_size);
    pc->entries[pc->entry_count].hash = hash;
    pc->entries[pc->entry_count].stage = stage;
    pc->entries[pc->entry_count].spirv_size = spirv_size;
    pc->entries[pc->entry_count].binary_size = (uint32_t)binary_size;
    pc->entries[pc->entry_count].binary = binary_copy;
    pc->entry_count++;
    return VK_SUCCESS;
}

/* Cache input and output are byte streams, potentially unaligned. */
static uint32_t vk_ps4_cache_u32(const uint8_t *data) {
    uint32_t value; memcpy(&value, data, sizeof(value)); return value;
}
static uint64_t vk_ps4_cache_u64(const uint8_t *data) {
    uint64_t value; memcpy(&value, data, sizeof(value)); return value;
}

/* Validate the entire stream before loading a single entry. A partial cache
 * after a malformed record would otherwise look successfully warmed. */
static bool vk_ps4_cache_data_valid(const uint8_t *data, size_t size) {
    if (size < VK_PS4_CACHE_HEADER_SIZE + 4u ||
        vk_ps4_cache_u32(data) != VK_PS4_CACHE_HEADER_SIZE ||
        vk_ps4_cache_u32(data + 4) != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
        vk_ps4_cache_u32(data + 8) != VK_PS4_CACHE_VENDOR_ID ||
        vk_ps4_cache_u32(data + 12) != VK_PS4_CACHE_DEVICE_ID ||
        memcmp(data + 16, vk_ps4_pipeline_cache_uuid, VK_UUID_SIZE))
        return false;
    data += VK_PS4_CACHE_HEADER_SIZE;
    size -= VK_PS4_CACHE_HEADER_SIZE;
    const uint32_t count = vk_ps4_cache_u32(data);
    data += 4; size -= 4;
    if (count > VK_PS4_PIPELINE_CACHE_MAX_ENTRIES) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (size < VK_PS4_CACHE_RECORD_SIZE) return false;
        const uint32_t stage = vk_ps4_cache_u32(data + 8);
        const uint32_t spirv_size = vk_ps4_cache_u32(data + 12);
        const uint32_t binary_size = vk_ps4_cache_u32(data + 16);
        if (!stage || (stage & (stage - 1u)) ||
            (stage & ~0x3fu) || spirv_size < 20 || (spirv_size & 3u))
            return false;
        data += VK_PS4_CACHE_RECORD_SIZE; size -= VK_PS4_CACHE_RECORD_SIZE;
        if (!binary_size || binary_size > size) return false;
        data += binary_size; size -= binary_size;
    }
    return size == 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache) {
    if (!device || !pPipelineCache) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4PipelineCache *cache = (VkPs4PipelineCache *)vk_ps4_alloc_zero(
        alloc, sizeof(VkPs4PipelineCache), 8);
    if (!cache) return VK_ERROR_OUT_OF_HOST_MEMORY;
    cache->type = VK_PS4_OBJ_PIPELINE_CACHE;
    cache->device = dev;
    cache->allocator = *alloc;
    cache->entry_count = 0;

    /* Incompatible or damaged disk caches are optional warm-start data.
     * Ignore them and compile clean shaders, rather than revive old ISA. */
    if (pCreateInfo && pCreateInfo->initialDataSize && pCreateInfo->pInitialData) {
        const uint8_t *data = pCreateInfo->pInitialData;
        if (vk_ps4_cache_data_valid(data, pCreateInfo->initialDataSize)) {
            data += VK_PS4_CACHE_HEADER_SIZE;
            const uint32_t count = vk_ps4_cache_u32(data); data += 4;
            for (uint32_t i = 0; i < count; ++i) {
                const uint64_t hash = vk_ps4_cache_u64(data);
                const uint32_t stage = vk_ps4_cache_u32(data + 8);
                const uint32_t spirv_size = vk_ps4_cache_u32(data + 12);
                const uint32_t binary_size = vk_ps4_cache_u32(data + 16);
                data += VK_PS4_CACHE_RECORD_SIZE;
                if (vk_ps4_pipeline_cache_insert((VkPipelineCache)cache, hash,
                        stage, spirv_size, data, binary_size) != VK_SUCCESS)
                    break; /* Valid partial warm start on allocation failure. */
                data += binary_size;
            }
            vk_ps4_log("PipelineCache: current ABI accepted entries=%u", cache->entry_count);
        } else {
            vk_ps4_log_raw("PipelineCache: incompatible or malformed initial data ignored; rebuilding shaders");
        }
    }

    *pPipelineCache = (VkPipelineCache)cache;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                             const VkAllocationCallbacks *pAllocator) {
    (void)device;
    if (!pipelineCache) return;
    VkPs4PipelineCache *cache = (VkPs4PipelineCache *)pipelineCache;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &cache->allocator;
    for (uint32_t i = 0; i < cache->entry_count; i++)
        vk_ps4_free(alloc, cache->entries[i].binary);
    vk_ps4_free(alloc, cache);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache,
                             size_t *pDataSize, void *pData) {
    (void)device;
    if (!pDataSize) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pipelineCache) {
        *pDataSize = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4PipelineCache *cache = (VkPs4PipelineCache *)pipelineCache;

    size_t total_size = VK_PS4_CACHE_HEADER_SIZE + 4u;
    for (uint32_t i = 0; i < cache->entry_count; ++i)
        total_size += VK_PS4_CACHE_RECORD_SIZE + cache->entries[i].binary_size;
    if (!pData) {
        *pDataSize = total_size;
        return VK_SUCCESS;
    }

    const size_t capacity = *pDataSize;
    if (capacity < VK_PS4_CACHE_HEADER_SIZE + 4u) {
        *pDataSize = 0; /* No complete header/payload count can be written. */
        return VK_INCOMPLETE;
    }
    uint8_t *out = pData;
    const uint32_t header[4] = {
        VK_PS4_CACHE_HEADER_SIZE, VK_PIPELINE_CACHE_HEADER_VERSION_ONE,
        VK_PS4_CACHE_VENDOR_ID, VK_PS4_CACHE_DEVICE_ID
    };
    memcpy(out, header, sizeof(header));
    memcpy(out + 16, vk_ps4_pipeline_cache_uuid, VK_UUID_SIZE);
    size_t written = VK_PS4_CACHE_HEADER_SIZE + 4u;
    uint32_t count = 0;
    for (uint32_t i = 0; i < cache->entry_count; ++i) {
        const VkPs4PipelineCacheEntry *entry = &cache->entries[i];
        const size_t record_size = VK_PS4_CACHE_RECORD_SIZE + entry->binary_size;
        if (record_size > capacity - written) break;
        uint8_t *record = out + written;
        memcpy(record, &entry->hash, 8);
        memcpy(record + 8, &entry->stage, 4);
        memcpy(record + 12, &entry->spirv_size, 4);
        memcpy(record + 16, &entry->binary_size, 4);
        memcpy(record + VK_PS4_CACHE_RECORD_SIZE, entry->binary, entry->binary_size);
        written += record_size;
        ++count;
    }
    memcpy(out + VK_PS4_CACHE_HEADER_SIZE, &count, 4);
    *pDataSize = written;
    return count == cache->entry_count ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
                            uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches) {
    (void)device;
    if (!dstCache) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4PipelineCache *dst = (VkPs4PipelineCache *)dstCache;

    for (uint32_t s = 0; s < srcCacheCount; s++) {
        if (!pSrcCaches[s]) continue;
        VkPs4PipelineCache *src = (VkPs4PipelineCache *)pSrcCaches[s];
        for (uint32_t i = 0; i < src->entry_count; i++) {
            if (dst->entry_count >= VK_PS4_PIPELINE_CACHE_MAX_ENTRIES) break;
            /* Check for duplicate */
            bool found = false;
            for (uint32_t j = 0; j < dst->entry_count; j++) {
                if (dst->entries[j].hash == src->entries[i].hash &&
                    dst->entries[j].stage == src->entries[i].stage) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                void *binary_copy = vk_ps4_alloc(&dst->allocator,
                    src->entries[i].binary_size, 16);
                if (!binary_copy) return VK_ERROR_OUT_OF_HOST_MEMORY;
                memcpy(binary_copy, src->entries[i].binary, src->entries[i].binary_size);
                dst->entries[dst->entry_count] = src->entries[i];
                dst->entries[dst->entry_count].binary = binary_copy;
                dst->entry_count++;
            }
        }
    }
    return VK_SUCCESS;
}

/* vkCmdBeginRenderPass2 / vkCmdNextSubpass2 / vkCmdEndRenderPass2 are the
 * Vulkan 1.2 variants of the render pass commands.  They take the same
 * VkRenderPassBeginInfo as v1, plus VkSubpassBeginInfo/VkSubpassEndInfo
 * which carry the `contents` field.  We delegate to the v1 implementations. */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                            const VkRenderPassBeginInfo *pRenderPassBegin,
                            const VkSubpassBeginInfo *pSubpassBeginInfo) {
    VkSubpassContents contents = pSubpassBeginInfo
        ? pSubpassBeginInfo->contents
        : VK_SUBPASS_CONTENTS_INLINE;
    vk_ps4_CmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                        const VkSubpassBeginInfo *pSubpassBeginInfo,
                        const VkSubpassEndInfo *pSubpassEndInfo) {
    VkSubpassContents contents = pSubpassBeginInfo
        ? pSubpassBeginInfo->contents
        : VK_SUBPASS_CONTENTS_INLINE;
    (void)pSubpassEndInfo;
    vk_ps4_CmdNextSubpass(commandBuffer, contents);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                          const VkSubpassEndInfo *pSubpassEndInfo) {
    (void)pSubpassEndInfo;
    vk_ps4_CmdEndRenderPass(commandBuffer);
}

STUB_NOT_IMPL(vk_ps4_QueueBindSparse, VkResult, VK_ERROR_FEATURE_NOT_PRESENT)

/* === Name → function lookup table === */
typedef struct {
    const char *name;
    PFN_vkVoidFunction func;
} VkPs4NameEntry;

#define ENTRY(name) { #name, (PFN_vkVoidFunction)name }

static const VkPs4NameEntry g_instance_funcs[] = {
    ENTRY(vkCreateInstance),
    ENTRY(vkDestroyInstance),
    ENTRY(vkEnumeratePhysicalDevices),
    ENTRY(vkGetPhysicalDeviceProperties),
    ENTRY(vkGetPhysicalDeviceMemoryProperties),
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties),
    ENTRY(vkGetPhysicalDeviceFeatures),
    ENTRY(vkGetPhysicalDeviceFormatProperties),
    ENTRY(vkGetInstanceProcAddr),
    ENTRY(vkEnumerateInstanceExtensionProperties),
    ENTRY(vkEnumerateInstanceLayerProperties),
    ENTRY(vkEnumerateDeviceExtensionProperties),
    ENTRY(vkEnumerateDeviceLayerProperties),
    ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties),
    ENTRY(vkGetPhysicalDeviceImageFormatProperties),
    ENTRY(vkCreateDevice),
    /* Vulkan 1.1 */
    ENTRY(vkEnumerateInstanceVersion),
    ENTRY(vkEnumeratePhysicalDeviceGroups),
    ENTRY(vkGetPhysicalDeviceProperties2),
    ENTRY(vkGetPhysicalDeviceFeatures2),
    ENTRY(vkGetPhysicalDeviceFormatProperties2),
    ENTRY(vkGetPhysicalDeviceImageFormatProperties2),
    ENTRY(vkGetPhysicalDeviceExternalBufferProperties),
    ENTRY(vkGetPhysicalDeviceExternalFenceProperties),
    ENTRY(vkGetPhysicalDeviceExternalSemaphoreProperties),
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties2),
    ENTRY(vkGetPhysicalDeviceMemoryProperties2),
    ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties2),
};

static const VkPs4NameEntry g_device_funcs[] = {
    ENTRY(vkDestroyDevice),
    ENTRY(vkGetDeviceQueue),
    ENTRY(vkGetDeviceProcAddr),
    ENTRY(vkAllocateMemory),
    ENTRY(vkFreeMemory),
    ENTRY(vkMapMemory),
    ENTRY(vkUnmapMemory),
    ENTRY(vkFlushMappedMemoryRanges),
    ENTRY(vkInvalidateMappedMemoryRanges),
    ENTRY(vkCreateBuffer),
    ENTRY(vkDestroyBuffer),
    ENTRY(vkGetBufferMemoryRequirements),
    ENTRY(vkBindBufferMemory),
    ENTRY(vkCreateImage),
    ENTRY(vkDestroyImage),
    ENTRY(vkGetImageMemoryRequirements),
    ENTRY(vkBindImageMemory),
    ENTRY(vkCreateImageView),
    ENTRY(vkDestroyImageView),
    ENTRY(vkCreateRenderPass),
    ENTRY(vkDestroyRenderPass),
    ENTRY(vkCreateFramebuffer),
    ENTRY(vkDestroyFramebuffer),
    ENTRY(vkCreateShaderModule),
    ENTRY(vkDestroyShaderModule),
    ENTRY(vkCreatePipelineLayout),
    ENTRY(vkDestroyPipelineLayout),
    ENTRY(vkCreateGraphicsPipelines),
    ENTRY(vkCreateComputePipelines),
    ENTRY(vkDestroyPipeline),
    ENTRY(vkCreateDescriptorSetLayout),
    ENTRY(vkDestroyDescriptorSetLayout),
    ENTRY(vkCreateDescriptorPool),
    ENTRY(vkDestroyDescriptorPool),
    ENTRY(vkAllocateDescriptorSets),
    ENTRY(vkFreeDescriptorSets),
    ENTRY(vkUpdateDescriptorSets),
    ENTRY(vkCreateCommandPool),
    ENTRY(vkDestroyCommandPool),
    ENTRY(vkAllocateCommandBuffers),
    ENTRY(vkFreeCommandBuffers),
    ENTRY(vkBeginCommandBuffer),
    ENTRY(vkEndCommandBuffer),
    ENTRY(vkResetCommandBuffer),
    ENTRY(vkQueueSubmit),
    ENTRY(vkQueueWaitIdle),
    ENTRY(vkDeviceWaitIdle),
    ENTRY(vkCreateFence),
    ENTRY(vkDestroyFence),
    ENTRY(vkWaitForFences),
    ENTRY(vkResetFences),
    ENTRY(vkGetFenceStatus),
    ENTRY(vkCreateSemaphore),
    ENTRY(vkDestroySemaphore),
    ENTRY(vkCreateEvent),
    ENTRY(vkDestroyEvent),
    ENTRY(vkGetEventStatus),
    ENTRY(vkSetEvent),
    ENTRY(vkResetEvent),
    ENTRY(vkCreateQueryPool),
    ENTRY(vkDestroyQueryPool),
    ENTRY(vkGetQueryPoolResults),
    ENTRY(vkCreateSwapchainKHR),
    ENTRY(vkDestroySwapchainKHR),
    ENTRY(vkGetSwapchainImagesKHR),
    ENTRY(vkAcquireNextImageKHR),
    ENTRY(vkQueuePresentKHR),
    ENTRY(vkCreateSampler),
    ENTRY(vkDestroySampler),
    ENTRY(vkResetCommandPool),
    ENTRY(vkTrimCommandPool),
    ENTRY(vkResetDescriptorPool),
    ENTRY(vkGetImageSubresourceLayout),
    ENTRY(vkCreateBufferView),
    ENTRY(vkDestroyBufferView),
    ENTRY(vkCreatePipelineCache),
    ENTRY(vkDestroyPipelineCache),
    ENTRY(vkGetPipelineCacheData),
    ENTRY(vkMergePipelineCaches),
    ENTRY(vkGetRenderAreaGranularity),
    ENTRY(vkGetDeviceMemoryCommitment),
    ENTRY(vkGetImageSparseMemoryRequirements),
    ENTRY(vkQueueBindSparse),
    /* Vulkan 1.1 */
    ENTRY(vkGetDeviceQueue2),
    ENTRY(vkBindBufferMemory2),
    ENTRY(vkBindImageMemory2),
    ENTRY(vkGetDeviceGroupPeerMemoryFeatures),
    ENTRY(vkGetDescriptorSetLayoutSupport),
    ENTRY(vkCreateDescriptorUpdateTemplate),
    ENTRY(vkDestroyDescriptorUpdateTemplate),
    ENTRY(vkUpdateDescriptorSetWithTemplate),
    ENTRY(vkGetBufferMemoryRequirements2),
    ENTRY(vkGetImageMemoryRequirements2),
    ENTRY(vkGetImageSparseMemoryRequirements2),
    ENTRY(vkCreateSamplerYcbcrConversion),
    ENTRY(vkDestroySamplerYcbcrConversion),
};

static const VkPs4NameEntry g_cmd_funcs[] = {
    ENTRY(vkCmdBindPipeline),
    ENTRY(vkCmdSetViewport),
    ENTRY(vkCmdSetScissor),
    ENTRY(vkCmdBindDescriptorSets),
    ENTRY(vkCmdBindVertexBuffers),
    ENTRY(vkCmdBindIndexBuffer),
    ENTRY(vkCmdDraw),
    ENTRY(vkCmdDrawIndexed),
    ENTRY(vkCmdDrawIndirect),
    ENTRY(vkCmdDrawIndexedIndirect),
    ENTRY(vkCmdDispatch),
    ENTRY(vkCmdCopyBuffer),
    ENTRY(vkCmdCopyImage),
    ENTRY(vkCmdBlitImage),
    ENTRY(vkCmdCopyBufferToImage),
    ENTRY(vkCmdCopyImageToBuffer),
    ENTRY(vkCmdBeginRenderPass),
    ENTRY(vkCmdEndRenderPass),
    ENTRY(vkCmdPipelineBarrier),
    ENTRY(vkCmdClearColorImage),
    ENTRY(vkCmdClearDepthStencilImage),
    ENTRY(vkCmdClearAttachments),
    ENTRY(vkCmdPushConstants),
    ENTRY(vkCmdResetQueryPool),
    ENTRY(vkCmdBeginQuery),
    ENTRY(vkCmdEndQuery),
    ENTRY(vkCmdWriteTimestamp),
    ENTRY(vkCmdSetLineWidth),
    ENTRY(vkCmdSetDepthBias),
    ENTRY(vkCmdSetBlendConstants),
    ENTRY(vkCmdSetDepthBounds),
    ENTRY(vkCmdSetStencilCompareMask),
    ENTRY(vkCmdSetStencilWriteMask),
    ENTRY(vkCmdSetStencilReference),
    ENTRY(vkCmdExecuteCommands),
    ENTRY(vkCmdNextSubpass),
    ENTRY(vkCmdBeginRenderPass2),
    ENTRY(vkCmdNextSubpass2),
    ENTRY(vkCmdEndRenderPass2),
    ENTRY(vkCmdCopyQueryPoolResults),
    ENTRY(vkCmdDispatchIndirect),
    ENTRY(vkCmdFillBuffer),
    ENTRY(vkCmdUpdateBuffer),
    ENTRY(vkCmdResolveImage),
    ENTRY(vkCmdWaitEvents),
    ENTRY(vkCmdResetEvent),
    ENTRY(vkCmdSetEvent),
    /* Vulkan 1.1 */
    ENTRY(vkCmdSetDeviceMask),
    ENTRY(vkCmdDispatchBase),
    /* VK_KHR_timeline_semaphore */
    ENTRY(vkGetSemaphoreCounterValueKHR),
    ENTRY(vkSignalSemaphoreKHR),
    ENTRY(vkWaitSemaphoresKHR),
    /* VK_KHR_create_renderpass2 */
    ENTRY(vkCreateRenderPass2),
    ENTRY(vkCreateRenderPass2KHR),
    ENTRY(vkCmdBeginRenderPass2),
    ENTRY(vkCmdBeginRenderPass2KHR),
    ENTRY(vkCmdNextSubpass2),
    ENTRY(vkCmdNextSubpass2KHR),
    ENTRY(vkCmdEndRenderPass2),
    ENTRY(vkCmdEndRenderPass2KHR),
    /* VK_EXT_host_query_reset */
    ENTRY(vkResetQueryPoolEXT),
    /* VK_KHR_buffer_device_address */
    ENTRY(vkGetBufferDeviceAddress),
    ENTRY(vkGetBufferDeviceAddressKHR),
};

/* === vkGetInstanceProcAddr === */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    (void)instance;
    if (!pName) return NULL;

    /* Check instance-level functions */
    for (size_t i = 0; i < sizeof(g_instance_funcs) / sizeof(g_instance_funcs[0]); i++) {
        if (strcmp(g_instance_funcs[i].name, pName) == 0) {
            return g_instance_funcs[i].func;
        }
    }
    /* Device-level functions are also retrievable via getInstanceProcAddr */
    for (size_t i = 0; i < sizeof(g_device_funcs) / sizeof(g_device_funcs[0]); i++) {
        if (strcmp(g_device_funcs[i].name, pName) == 0) {
            return g_device_funcs[i].func;
        }
    }
    /* Command buffer functions */
    for (size_t i = 0; i < sizeof(g_cmd_funcs) / sizeof(g_cmd_funcs[0]); i++) {
        if (strcmp(g_cmd_funcs[i].name, pName) == 0) {
            return g_cmd_funcs[i].func;
        }
    }
    return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}

/* === vkGetDeviceProcAddr === */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    (void)device;
    if (!pName) return NULL;

    for (size_t i = 0; i < sizeof(g_device_funcs) / sizeof(g_device_funcs[0]); i++) {
        if (strcmp(g_device_funcs[i].name, pName) == 0) {
            return g_device_funcs[i].func;
        }
    }
    for (size_t i = 0; i < sizeof(g_cmd_funcs) / sizeof(g_cmd_funcs[0]); i++) {
        if (strcmp(g_cmd_funcs[i].name, pName) == 0) {
            return g_cmd_funcs[i].func;
        }
    }
    return NULL;
}
