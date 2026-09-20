// Executes the bundled VMA allocator's real selection with actual ICD properties.
// No GPU allocation or GPU pixel claim: unused Vulkan calls abort if reached.
#define VMA_VULKAN_VERSION 1000000
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "rendering/ps4_shadow_memory.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
extern "C" {
VkResult vk_ps4_CreateInstance(const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
void vk_ps4_DestroyInstance(VkInstance,const VkAllocationCallbacks*);
VkResult vk_ps4_EnumeratePhysicalDevices(VkInstance,uint32_t*,VkPhysicalDevice*);
void vk_ps4_GetPhysicalDeviceProperties(VkPhysicalDevice,VkPhysicalDeviceProperties*);
void vk_ps4_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties*);
void *vk_ps4_alloc_zero(const VkAllocationCallbacks*,size_t size,size_t) { return calloc(1,size); }
void vk_ps4_free(const VkAllocationCallbacks*,void* p) { free(p); }
bool vk_ps4_log_is_open(void) { return true; }
void vk_ps4_log_open(const char*) {}
void vk_ps4_log_raw(const char*) {}
}
static void unused() { std::abort(); }
int main() {
    VkInstance instance{};
    VkInstanceCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    assert(vk_ps4_CreateInstance(&ci,nullptr,&instance)==VK_SUCCESS);
    uint32_t count=1; VkPhysicalDevice physical{};
    assert(vk_ps4_EnumeratePhysicalDevices(instance,&count,&physical)==VK_SUCCESS);
    VkPhysicalDeviceMemoryProperties props{};
    vk_ps4_GetPhysicalDeviceMemoryProperties(physical,&props);
    assert(props.memoryTypeCount==2);
    const uint32_t allowed = wowee::rendering::ps4ShadowMemoryTypeBits(props);
    assert(allowed==2u);
    VmaVulkanFunctions f{};
    f.vkGetPhysicalDeviceProperties=vk_ps4_GetPhysicalDeviceProperties;
    f.vkGetPhysicalDeviceMemoryProperties=vk_ps4_GetPhysicalDeviceMemoryProperties;
#define UNUSED_FN(name) f.name=reinterpret_cast<PFN_##name>(unused)
    UNUSED_FN(vkAllocateMemory); UNUSED_FN(vkFreeMemory);
    UNUSED_FN(vkMapMemory); UNUSED_FN(vkUnmapMemory);
    UNUSED_FN(vkFlushMappedMemoryRanges); UNUSED_FN(vkInvalidateMappedMemoryRanges);
    UNUSED_FN(vkBindBufferMemory); UNUSED_FN(vkBindImageMemory);
    UNUSED_FN(vkGetBufferMemoryRequirements); UNUSED_FN(vkGetImageMemoryRequirements);
    UNUSED_FN(vkCreateBuffer); UNUSED_FN(vkDestroyBuffer);
    UNUSED_FN(vkCreateImage); UNUSED_FN(vkDestroyImage); UNUSED_FN(vkCmdCopyBuffer);
#undef UNUSED_FN
    VmaAllocatorCreateInfo aci{}; aci.instance=instance; aci.physicalDevice=physical;
    aci.device=reinterpret_cast<VkDevice>(physical); // never dereferenced: selection only
    aci.vulkanApiVersion=VK_API_VERSION_1_0; aci.pVulkanFunctions=&f;
    VmaAllocator allocator{}; assert(vmaCreateAllocator(&aci,&allocator)==VK_SUCCESS);
    VmaAllocationCreateInfo allocation{}; allocation.usage=VMA_MEMORY_USAGE_GPU_ONLY;
    uint32_t oldType=UINT32_MAX,newType=UINT32_MAX;
    assert(vmaFindMemoryTypeIndex(allocator,3,&allocation,&oldType)==VK_SUCCESS);
    assert(oldType==0); // regression: DEVICE_LOCAL tie selects cached Onion first
    allocation.memoryTypeBits=allowed;
    assert(vmaFindMemoryTypeIndex(allocator,3,&allocation,&newType)==VK_SUCCESS);
    assert(newType==1); // production policy now requires Garlic
    assert(vmaFindMemoryTypeIndex(allocator,1,&allocation,&newType)==VK_ERROR_FEATURE_NOT_PRESENT);
    auto cachedOnly=props; cachedOnly.memoryTypeCount=1;
    assert(wowee::rendering::ps4ShadowMemoryTypeBits(cachedOnly)==0);
    auto empty=VkPhysicalDeviceMemoryProperties{};
    assert(wowee::rendering::ps4ShadowMemoryTypeBits(empty)==0);
    // The application must reject an empty mask before VMA (0 means unrestricted).
    allocation.memoryTypeBits=0;
    assert(vmaFindMemoryTypeIndex(allocator,3,&allocation,&newType)==VK_SUCCESS && newType==0);
    printf("PASS real ICD + VMA: GPU_ONLY old type=%u flags=%u; restricted type=1 flags=%u; mask=%u\n",
        oldType,props.memoryTypes[0].propertyFlags,props.memoryTypes[1].propertyFlags,allowed);
    puts("PASS excluded Garlic rejects; cached-only/empty policy mask=0; VMA zero-mask hazard reproduced");
    vmaDestroyAllocator(allocator); vk_ps4_DestroyInstance(instance,nullptr);
}
