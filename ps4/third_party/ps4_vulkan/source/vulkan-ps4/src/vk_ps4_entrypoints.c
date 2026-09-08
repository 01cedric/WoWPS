/*
 * vk_ps4_entrypoints.c — Public vk* entry point symbols.
 *
 * The Vulkan ICD must export symbols named vkCreateInstance, vkDestroyDevice,
 * vkCmdDraw, etc. Our implementations are named vk_ps4_* (defined in
 * vk_ps4_instance.c, vk_ps4_stubs.c, etc.). This file creates thin forwarding
 * wrappers that export the canonical vk* names.
 *
 * vkGetInstanceProcAddr and vkGetDeviceProcAddr are already defined in
 * vk_ps4_dispatch.c.
 */

#include "vk_ps4.h"
#include "vk_ps4_internal.h"

/* === Instance === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *a, const VkAllocationCallbacks *b, VkInstance *c) { return vk_ps4_CreateInstance(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance a, const VkAllocationCallbacks *b) { vk_ps4_DestroyInstance(a, b); }
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance a, uint32_t *b, VkPhysicalDevice *c) { return vk_ps4_EnumeratePhysicalDevices(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice a, VkPhysicalDeviceProperties *b) { vk_ps4_GetPhysicalDeviceProperties(a, b); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice a, VkPhysicalDeviceMemoryProperties *b) { vk_ps4_GetPhysicalDeviceMemoryProperties(a, b); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice a, uint32_t *b, VkQueueFamilyProperties *c) { vk_ps4_GetPhysicalDeviceQueueFamilyProperties(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice a, VkPhysicalDeviceFeatures *b) { vk_ps4_GetPhysicalDeviceFeatures(a, b); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice a, VkFormat b, VkFormatProperties *c) { vk_ps4_GetPhysicalDeviceFormatProperties(a, b, c); }

/* === Device === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice a, const VkDeviceCreateInfo *b, const VkAllocationCallbacks *c, VkDevice *d) { return vk_ps4_CreateDevice(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice a, const VkAllocationCallbacks *b) { vk_ps4_DestroyDevice(a, b); }
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice a, uint32_t b, uint32_t c, VkQueue *d) { vk_ps4_GetDeviceQueue(a, b, c, d); }

/* === Memory === */
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice a, const VkMemoryAllocateInfo *b, const VkAllocationCallbacks *c, VkDeviceMemory *d) { return vk_ps4_AllocateMemory(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice a, VkDeviceMemory b, const VkAllocationCallbacks *c) { vk_ps4_FreeMemory(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice a, VkDeviceMemory b, VkDeviceSize c, VkDeviceSize d, VkMemoryMapFlags e, void **f) { return vk_ps4_MapMemory(a, b, c, d, e, f); }
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice a, VkDeviceMemory b) { vk_ps4_UnmapMemory(a, b); }
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice a, uint32_t b, const VkMappedMemoryRange *c) { return vk_ps4_FlushMappedMemoryRanges(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice a, uint32_t b, const VkMappedMemoryRange *c) { return vk_ps4_InvalidateMappedMemoryRanges(a, b, c); }

/* === Buffer === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice a, const VkBufferCreateInfo *b, const VkAllocationCallbacks *c, VkBuffer *d) { return vk_ps4_CreateBuffer(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice a, VkBuffer b, const VkAllocationCallbacks *c) { vk_ps4_DestroyBuffer(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice a, VkBuffer b, VkMemoryRequirements *c) { vk_ps4_GetBufferMemoryRequirements(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice a, VkBuffer b, VkDeviceMemory c, VkDeviceSize d) { return vk_ps4_BindBufferMemory(a, b, c, d); }

/* === Image === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice a, const VkImageCreateInfo *b, const VkAllocationCallbacks *c, VkImage *d) { return vk_ps4_CreateImage(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice a, VkImage b, const VkAllocationCallbacks *c) { vk_ps4_DestroyImage(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice a, VkImage b, VkMemoryRequirements *c) { vk_ps4_GetImageMemoryRequirements(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice a, VkImage b, VkDeviceMemory c, VkDeviceSize d) { return vk_ps4_BindImageMemory(a, b, c, d); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice a, const VkImageViewCreateInfo *b, const VkAllocationCallbacks *c, VkImageView *d) { return vk_ps4_CreateImageView(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice a, VkImageView b, const VkAllocationCallbacks *c) { vk_ps4_DestroyImageView(a, b, c); }

/* === Render pass / framebuffer === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice a, const VkRenderPassCreateInfo *b, const VkAllocationCallbacks *c, VkRenderPass *d) { return vk_ps4_CreateRenderPass(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice a, VkRenderPass b, const VkAllocationCallbacks *c) { vk_ps4_DestroyRenderPass(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice a, const VkFramebufferCreateInfo *b, const VkAllocationCallbacks *c, VkFramebuffer *d) { return vk_ps4_CreateFramebuffer(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice a, VkFramebuffer b, const VkAllocationCallbacks *c) { vk_ps4_DestroyFramebuffer(a, b, c); }

/* === Shader / pipeline === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice a, const VkShaderModuleCreateInfo *b, const VkAllocationCallbacks *c, VkShaderModule *d) { return vk_ps4_CreateShaderModule(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice a, VkShaderModule b, const VkAllocationCallbacks *c) { vk_ps4_DestroyShaderModule(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice a, const VkPipelineLayoutCreateInfo *b, const VkAllocationCallbacks *c, VkPipelineLayout *d) { return vk_ps4_CreatePipelineLayout(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice a, VkPipelineLayout b, const VkAllocationCallbacks *c) { vk_ps4_DestroyPipelineLayout(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice a, VkPipelineCache b, uint32_t c, const VkGraphicsPipelineCreateInfo *d, const VkAllocationCallbacks *e, VkPipeline *f) { return vk_ps4_CreateGraphicsPipelines(a, b, c, d, e, f); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice a, VkPipelineCache b, uint32_t c, const VkComputePipelineCreateInfo *d, const VkAllocationCallbacks *e, VkPipeline *f) { return vk_ps4_CreateComputePipelines(a, b, c, d, e, f); }
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice a, VkPipeline b, const VkAllocationCallbacks *c) { vk_ps4_DestroyPipeline(a, b, c); }

/* === Descriptor === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice a, const VkDescriptorSetLayoutCreateInfo *b, const VkAllocationCallbacks *c, VkDescriptorSetLayout *d) { return vk_ps4_CreateDescriptorSetLayout(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice a, VkDescriptorSetLayout b, const VkAllocationCallbacks *c) { vk_ps4_DestroyDescriptorSetLayout(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice a, const VkDescriptorPoolCreateInfo *b, const VkAllocationCallbacks *c, VkDescriptorPool *d) { return vk_ps4_CreateDescriptorPool(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice a, VkDescriptorPool b, const VkAllocationCallbacks *c) { vk_ps4_DestroyDescriptorPool(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice a, const VkDescriptorSetAllocateInfo *b, VkDescriptorSet *c) { return vk_ps4_AllocateDescriptorSets(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice a, VkDescriptorPool b, uint32_t c, const VkDescriptorSet *d) { return vk_ps4_FreeDescriptorSets(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice a, uint32_t b, const VkWriteDescriptorSet *c, uint32_t d, const VkCopyDescriptorSet *e) { vk_ps4_UpdateDescriptorSets(a, b, c, d, e); }

/* === Command buffer === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice a, const VkCommandPoolCreateInfo *b, const VkAllocationCallbacks *c, VkCommandPool *d) { return vk_ps4_CreateCommandPool(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice a, VkCommandPool b, const VkAllocationCallbacks *c) { vk_ps4_DestroyCommandPool(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice a, const VkCommandBufferAllocateInfo *b, VkCommandBuffer *c) { return vk_ps4_AllocateCommandBuffers(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice a, VkCommandPool b, uint32_t c, const VkCommandBuffer *d) { vk_ps4_FreeCommandBuffers(a, b, c, d); }
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer a, const VkCommandBufferBeginInfo *b) { return vk_ps4_BeginCommandBuffer(a, b); }
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer a) { return vk_ps4_EndCommandBuffer(a); }
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer a, VkCommandBufferResetFlags b) { return vk_ps4_ResetCommandBuffer(a, b); }

/* === Command buffer recording === */
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer a, VkPipelineBindPoint b, VkPipeline c) { vk_ps4_CmdBindPipeline(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer a, uint32_t b, uint32_t c, const VkViewport *d) { vk_ps4_CmdSetViewport(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer a, uint32_t b, uint32_t c, const VkRect2D *d) { vk_ps4_CmdSetScissor(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer a, VkPipelineBindPoint b, VkPipelineLayout c, uint32_t d, uint32_t e, const VkDescriptorSet *f, uint32_t g, const uint32_t *h) { vk_ps4_CmdBindDescriptorSets(a, b, c, d, e, f, g, h); }
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer a, uint32_t b, uint32_t c, const VkBuffer *d, const VkDeviceSize *e) { vk_ps4_CmdBindVertexBuffers(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer a, VkBuffer b, VkDeviceSize c, VkIndexType d) { vk_ps4_CmdBindIndexBuffer(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { vk_ps4_CmdDraw(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d, int32_t e, uint32_t f) { vk_ps4_CmdDrawIndexed(a, b, c, d, e, f); }
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer a, VkBuffer b, VkDeviceSize c, uint32_t d, uint32_t e) { vk_ps4_CmdDrawIndirect(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer a, VkBuffer b, VkDeviceSize c, uint32_t d, uint32_t e) { vk_ps4_CmdDrawIndexedIndirect(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d) { vk_ps4_CmdDispatch(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer a, VkBuffer b, VkBuffer c, uint32_t d, const VkBufferCopy *e) { vk_ps4_CmdCopyBuffer(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer a, VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f, const VkImageCopy *g) { vk_ps4_CmdCopyImage(a, b, c, d, e, f, g); }
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer a, VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f, const VkImageBlit *g, VkFilter h) { vk_ps4_CmdBlitImage(a, b, c, d, e, f, g, h); }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer a, VkBuffer b, VkImage c, VkImageLayout d, uint32_t e, const VkBufferImageCopy *f) { vk_ps4_CmdCopyBufferToImage(a, b, c, d, e, f); }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer a, VkImage b, VkImageLayout c, VkBuffer d, uint32_t e, const VkBufferImageCopy *f) { vk_ps4_CmdCopyImageToBuffer(a, b, c, d, e, f); }
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer a, const VkRenderPassBeginInfo *b, VkSubpassContents c) { vk_ps4_CmdBeginRenderPass(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer a, VkSubpassContents b) { vk_ps4_CmdNextSubpass(a, b); }
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer a) { vk_ps4_CmdEndRenderPass(a); }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer a, VkPipelineStageFlags b, VkPipelineStageFlags c, VkDependencyFlags d, uint32_t e, const VkMemoryBarrier *f, uint32_t g, const VkBufferMemoryBarrier *h, uint32_t i, const VkImageMemoryBarrier *j) { vk_ps4_CmdPipelineBarrier(a, b, c, d, e, f, g, h, i, j); }
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer a, VkImage b, VkImageLayout c, const VkClearColorValue *d, uint32_t e, const VkImageSubresourceRange *f) { vk_ps4_CmdClearColorImage(a, b, c, d, e, f); }
VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer a, VkImage b, VkImageLayout c, const VkClearDepthStencilValue *d, uint32_t e, const VkImageSubresourceRange *f) { vk_ps4_CmdClearDepthStencilImage(a, b, c, d, e, f); }
VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer a, uint32_t b, const VkClearAttachment *c, uint32_t d, const VkClearRect *e) { vk_ps4_CmdClearAttachments(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer a, VkPipelineLayout b, VkShaderStageFlags c, uint32_t d, uint32_t e, const void *f) { vk_ps4_CmdPushConstants(a, b, c, d, e, f); }

/* === Queue === */
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue a, uint32_t b, const VkSubmitInfo *c, VkFence d) { return vk_ps4_QueueSubmit(a, b, c, d); }
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue a) { return vk_ps4_QueueWaitIdle(a); }
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice a) { return vk_ps4_DeviceWaitIdle(a); }

/* === Sync === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice a, const VkFenceCreateInfo *b, const VkAllocationCallbacks *c, VkFence *d) { return vk_ps4_CreateFence(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice a, VkFence b, const VkAllocationCallbacks *c) { vk_ps4_DestroyFence(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice a, uint32_t b, const VkFence *c, VkBool32 d, uint64_t e) { return vk_ps4_WaitForFences(a, b, c, d, e); }
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice a, uint32_t b, const VkFence *c) { return vk_ps4_ResetFences(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice a, VkFence b) { return vk_ps4_GetFenceStatus(a, b); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice a, const VkSemaphoreCreateInfo *b, const VkAllocationCallbacks *c, VkSemaphore *d) { return vk_ps4_CreateSemaphore(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice a, VkSemaphore b, const VkAllocationCallbacks *c) { vk_ps4_DestroySemaphore(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(VkDevice a, const VkEventCreateInfo *b, const VkAllocationCallbacks *c, VkEvent *d) { return vk_ps4_CreateEvent(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(VkDevice a, VkEvent b, const VkAllocationCallbacks *c) { vk_ps4_DestroyEvent(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice a, VkEvent b) { return vk_ps4_GetEventStatus(a, b); }
VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice a, VkEvent b) { return vk_ps4_SetEvent(a, b); }
VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice a, VkEvent b) { return vk_ps4_ResetEvent(a, b); }

/* === Query === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice a, const VkQueryPoolCreateInfo *b, const VkAllocationCallbacks *c, VkQueryPool *d) { return vk_ps4_CreateQueryPool(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice a, VkQueryPool b, const VkAllocationCallbacks *c) { vk_ps4_DestroyQueryPool(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice a, VkQueryPool b, uint32_t c, uint32_t d, size_t e, void *f, VkDeviceSize g, VkQueryResultFlags h) { return vk_ps4_GetQueryPoolResults(a, b, c, d, e, f, g, h); }
VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer a, VkQueryPool b, uint32_t c, uint32_t d) { vk_ps4_CmdResetQueryPool(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkResetQueryPoolEXT(VkDevice a, VkQueryPool b, uint32_t c, uint32_t d) { vk_ps4_ResetQueryPoolEXT(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer a, VkQueryPool b, uint32_t c, VkQueryControlFlags d) { vk_ps4_CmdBeginQuery(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer a, VkQueryPool b, uint32_t c) { vk_ps4_CmdEndQuery(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer a, VkPipelineStageFlagBits b, VkQueryPool c, uint32_t d) { vk_ps4_CmdWriteTimestamp(a, b, c, d); }

/* === Swapchain === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice a, const VkSwapchainCreateInfoKHR *b, const VkAllocationCallbacks *c, VkSwapchainKHR *d) { return vk_ps4_CreateSwapchainKHR(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice a, VkSwapchainKHR b, const VkAllocationCallbacks *c) { vk_ps4_DestroySwapchainKHR(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice a, VkSwapchainKHR b, uint32_t *c, VkImage *d) { return vk_ps4_GetSwapchainImagesKHR(a, b, c, d); }
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice a, VkSwapchainKHR b, uint64_t c, VkSemaphore d, VkFence e, uint32_t *f) { return vk_ps4_AcquireNextImageKHR(a, b, c, d, e, f); }
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue a, const VkPresentInfoKHR *b) { return vk_ps4_QueuePresentKHR(a, b); }

/* === Sampler === */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice a, const VkSamplerCreateInfo *b, const VkAllocationCallbacks *c, VkSampler *d) { return vk_ps4_CreateSampler(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice a, VkSampler b, const VkAllocationCallbacks *c) { vk_ps4_DestroySampler(a, b, c); }

/* === Command pool management === */
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice a, VkCommandPool b, VkCommandPoolResetFlags c) { return vk_ps4_ResetCommandPool(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(VkDevice a, VkCommandPool b, VkCommandPoolTrimFlags c) { vk_ps4_TrimCommandPool(a, b, c); }

/* === Descriptor pool management === */
VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice a, VkDescriptorPool b, VkDescriptorPoolResetFlags c) { return vk_ps4_ResetDescriptorPool(a, b, c); }

/* === Image subresource layout === */
VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(VkDevice a, VkImage b, const VkImageSubresource *c, VkSubresourceLayout *d) { vk_ps4_GetImageSubresourceLayout(a, b, c, d); }

/* === Enumerate (stubs) === */
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *a, uint32_t *b, VkExtensionProperties *c) {
    return vk_icdEnumerateInstanceExtensionProperties(a, b, c);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *a, VkLayerProperties *b) {
    return vk_icdEnumerateInstanceLayerProperties(a, b);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice a, const char *b, uint32_t *c, VkExtensionProperties *d) {
    (void)a;
    return vk_ps4_enumerate_device_extensions(b, c, d);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice a, uint32_t *b, VkLayerProperties *c) {
    (void)a;
    return vk_icdEnumerateInstanceLayerProperties(b, c);
}

/* === Remaining stub entrypoints (return VK_ERROR_FEATURE_NOT_PRESENT or no-op) === */
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice a, VkFormat b, VkImageType c, VkSampleCountFlagBits d, VkImageUsageFlags e, VkImageTiling f, uint32_t *g, VkSparseImageFormatProperties *h) {
    vk_ps4_GetPhysicalDeviceSparseImageFormatProperties(a, b, c, d, e, f, g, h);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice a, VkFormat b, VkImageType c, VkImageTiling d, VkImageUsageFlags e, VkImageCreateFlags f, VkImageFormatProperties *g) {
    return vk_ps4_GetPhysicalDeviceImageFormatProperties(a, b, c, d, e, f, g);
}
/* Vulkan 1.1+ functions — not implemented, signatures differ from 1.0 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(VkDevice a, const VkBufferViewCreateInfo *b, const VkAllocationCallbacks *c, VkBufferView *d) { return vk_ps4_CreateBufferView(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(VkDevice a, VkBufferView b, const VkAllocationCallbacks *c) { vk_ps4_DestroyBufferView(a, b, c); }
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(VkDevice a, const VkBufferDeviceAddressInfo *b) { return vk_ps4_GetBufferDeviceAddress(a, b); }
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressKHR(VkDevice a, const VkBufferDeviceAddressInfo *b) { return vk_ps4_GetBufferDeviceAddress(a, b); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice a, const VkPipelineCacheCreateInfo *b, const VkAllocationCallbacks *c, VkPipelineCache *d) { return vk_ps4_CreatePipelineCache(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice a, VkPipelineCache b, const VkAllocationCallbacks *c) { vk_ps4_DestroyPipelineCache(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice a, VkPipelineCache b, size_t *c, void *d) { return vk_ps4_GetPipelineCacheData(a, b, c, d); }
VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(VkDevice a, VkPipelineCache b, uint32_t c, const VkPipelineCache *d) { return vk_ps4_MergePipelineCaches(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(VkDevice a, VkRenderPass b, VkExtent2D *c) { vk_ps4_GetRenderAreaGranularity(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer a, float b) { vk_ps4_CmdSetLineWidth(a, b); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer a, float b, float c, float d) { vk_ps4_CmdSetDepthBias(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer a, const float b[4]) { vk_ps4_CmdSetBlendConstants(a, b); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer a, float b, float c) { vk_ps4_CmdSetDepthBounds(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer a, VkStencilFaceFlags b, uint32_t c) { vk_ps4_CmdSetStencilCompareMask(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer a, VkStencilFaceFlags b, uint32_t c) { vk_ps4_CmdSetStencilWriteMask(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer a, VkStencilFaceFlags b, uint32_t c) { vk_ps4_CmdSetStencilReference(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(VkCommandBuffer a, uint32_t b, const VkCommandBuffer *c) { vk_ps4_CmdExecuteCommands(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(VkCommandBuffer a, VkQueryPool b, uint32_t c, uint32_t d, VkBuffer e, VkDeviceSize f, VkDeviceSize g, VkQueryResultFlags h) { vk_ps4_CmdCopyQueryPoolResults(a, b, c, d, e, f, g, h); }
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer a, VkBuffer b, VkDeviceSize c) { vk_ps4_CmdDispatchIndirect(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer a, VkBuffer b, VkDeviceSize c, VkDeviceSize d, uint32_t e) { vk_ps4_CmdFillBuffer(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer a, VkBuffer b, VkDeviceSize c, VkDeviceSize d, const void *e) { vk_ps4_CmdUpdateBuffer(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer a, VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f, const VkImageResolve *g) { vk_ps4_CmdResolveImage(a, b, c, d, e, f, g); }
/* VK_KHR_timeline_semaphore */
VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValueKHR(VkDevice a, VkSemaphore b, uint64_t *c) { return vk_ps4_GetSemaphoreCounterValueKHR(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphoreKHR(VkDevice a, const VkSemaphoreSignalInfoKHR *b) { return vk_ps4_SignalSemaphoreKHR(a, b); }
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphoresKHR(VkDevice a, const VkSemaphoreWaitInfoKHR *b, uint64_t c) { return vk_ps4_WaitSemaphoresKHR(a, b, c); }
/* Vulkan 1.2 render pass 2 variants */
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(VkCommandBuffer a, const VkRenderPassBeginInfo *b, const VkSubpassBeginInfo *c) { vk_ps4_CmdBeginRenderPass2(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2KHR(VkCommandBuffer a, const VkRenderPassBeginInfo *b, const VkSubpassBeginInfo *c) { vk_ps4_CmdBeginRenderPass2(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice a, const VkRenderPassCreateInfo2 *b, const VkAllocationCallbacks *c, VkRenderPass *d) { return vk_ps4_CreateRenderPass2(a, b, c, d); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2KHR(VkDevice a, const VkRenderPassCreateInfo2 *b, const VkAllocationCallbacks *c, VkRenderPass *d) { return vk_ps4_CreateRenderPass2(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(VkCommandBuffer a, const VkSubpassBeginInfo *b, const VkSubpassEndInfo *c) { vk_ps4_CmdNextSubpass2(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2KHR(VkCommandBuffer a, const VkSubpassBeginInfo *b, const VkSubpassEndInfo *c) { vk_ps4_CmdNextSubpass2(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer a, const VkSubpassEndInfo *b) { vk_ps4_CmdEndRenderPass2(a, b); }
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2KHR(VkCommandBuffer a, const VkSubpassEndInfo *b) { vk_ps4_CmdEndRenderPass2(a, b); }
VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(VkCommandBuffer a, uint32_t b, const VkEvent *c, VkPipelineStageFlags d, VkPipelineStageFlags e, uint32_t f, const VkMemoryBarrier *g, uint32_t h, const VkBufferMemoryBarrier *i, uint32_t j, const VkImageMemoryBarrier *k) { vk_ps4_CmdWaitEvents(a, b, c, d, e, f, g, h, i, j, k); }
VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(VkCommandBuffer a, VkEvent b, VkPipelineStageFlags c) { vk_ps4_CmdResetEvent(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(VkCommandBuffer a, VkEvent b, VkPipelineStageFlags c) { vk_ps4_CmdSetEvent(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(VkDevice a, VkDeviceMemory b, VkDeviceSize *c) { vk_ps4_GetDeviceMemoryCommitment(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(VkDevice a, VkImage b, uint32_t *c, VkSparseImageMemoryRequirements *d) { (void)a;(void)b; if(c){*c=0;} (void)d; }
VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2(VkDevice a, const VkImageSparseMemoryRequirementsInfo2 *b, uint32_t *c, VkSparseImageMemoryRequirements2 *d) { vk_ps4_GetImageSparseMemoryRequirements2(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice a, const VkBufferMemoryRequirementsInfo2 *b, VkMemoryRequirements2 *c) { vk_ps4_GetBufferMemoryRequirements2(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice a, const VkImageMemoryRequirementsInfo2 *b, VkMemoryRequirements2 *c) { vk_ps4_GetImageMemoryRequirements2(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(VkQueue a, uint32_t b, const VkBindSparseInfo *c, VkFence d) { (void)a;(void)b;(void)c;(void)d; return VK_ERROR_FEATURE_NOT_PRESENT; }

/* === Vulkan 1.1 entrypoints === */
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *a) { return vk_ps4_EnumerateInstanceVersion(a); }
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(VkInstance a, uint32_t *b, VkPhysicalDeviceGroupProperties *c) { return vk_ps4_EnumeratePhysicalDeviceGroups(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice a, VkPhysicalDeviceProperties2 *b) { vk_ps4_GetPhysicalDeviceProperties2(a, b); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice a, VkPhysicalDeviceFeatures2 *b) { vk_ps4_GetPhysicalDeviceFeatures2(a, b); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice a, VkFormat b, VkFormatProperties2 *c) { vk_ps4_GetPhysicalDeviceFormatProperties2(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice a, const VkPhysicalDeviceImageFormatInfo2 *b, VkImageFormatProperties2 *c) { return vk_ps4_GetPhysicalDeviceImageFormatProperties2(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice a, uint32_t *b, VkQueueFamilyProperties2 *c) { vk_ps4_GetPhysicalDeviceQueueFamilyProperties2(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice a, const VkPhysicalDeviceExternalBufferInfo *b, VkExternalBufferProperties *c) { vk_ps4_GetPhysicalDeviceExternalBufferProperties(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice a, const VkPhysicalDeviceExternalFenceInfo *b, VkExternalFenceProperties *c) { vk_ps4_GetPhysicalDeviceExternalFenceProperties(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice a, const VkPhysicalDeviceExternalSemaphoreInfo *b, VkExternalSemaphoreProperties *c) { vk_ps4_GetPhysicalDeviceExternalSemaphoreProperties(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice a, VkPhysicalDeviceMemoryProperties2 *b) { vk_ps4_GetPhysicalDeviceMemoryProperties2(a, b); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice a, const VkPhysicalDeviceSparseImageFormatInfo2 *b, uint32_t *c, VkSparseImageFormatProperties2 *d) { vk_ps4_GetPhysicalDeviceSparseImageFormatProperties2(a, b, c, d); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversion(VkDevice a, const VkSamplerYcbcrConversionCreateInfo *b, const VkAllocationCallbacks *c, VkSamplerYcbcrConversion *d) { return vk_ps4_CreateSamplerYcbcrConversion(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversion(VkDevice a, VkSamplerYcbcrConversion b, const VkAllocationCallbacks *c) { vk_ps4_DestroySamplerYcbcrConversion(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice a, const VkDeviceQueueInfo2 *b, VkQueue *c) { vk_ps4_GetDeviceQueue2(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice a, uint32_t b, const VkBindBufferMemoryInfo *c) { return vk_ps4_BindBufferMemory2(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice a, uint32_t b, const VkBindImageMemoryInfo *c) { return vk_ps4_BindImageMemory2(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeatures(VkDevice a, uint32_t b, uint32_t c, uint32_t d, VkPeerMemoryFeatureFlags *e) { vk_ps4_GetDeviceGroupPeerMemoryFeatures(a, b, c, d, e); }
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupport(VkDevice a, const VkDescriptorSetLayoutCreateInfo *b, VkDescriptorSetLayoutSupport *c) { vk_ps4_GetDescriptorSetLayoutSupport(a, b, c); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice a, const VkDescriptorUpdateTemplateCreateInfo *b, const VkAllocationCallbacks *c, VkDescriptorUpdateTemplate *d) { return vk_ps4_CreateDescriptorUpdateTemplate(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice a, VkDescriptorUpdateTemplate b, const VkAllocationCallbacks *c) { vk_ps4_DestroyDescriptorUpdateTemplate(a, b, c); }
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice a, VkDescriptorSet b, VkDescriptorUpdateTemplate c, const void *d) { vk_ps4_UpdateDescriptorSetWithTemplate(a, b, c, d); }
VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMask(VkCommandBuffer a, uint32_t b) { vk_ps4_CmdSetDeviceMask(a, b); }
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { vk_ps4_CmdDispatchBase(a, b, c, d, e, f, g); }
