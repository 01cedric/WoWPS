// Executes the bundled VMA allocator's real selection with actual ICD properties.
// No GPU allocation or GPU pixel claim: unused Vulkan calls abort if reached.
#define VMA_VULKAN_VERSION 1000000
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "rendering/ps4_shadow_memory.hpp"
#include "rendering/ps4_attachment_memory.hpp"
#include <vector>
#include <utility>
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
    using namespace wowee::rendering;
    constexpr VkImageUsageFlags attachments[] = {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    for (auto usage : attachments) {
        const uint32_t mask=ps4AttachmentMemoryTypeBits(usage,props);
        assert(mask==2u);
        allocation.memoryTypeBits=mask;
        uint32_t selected=99;
        assert(vmaFindMemoryTypeIndex(allocator,3,&allocation,&selected)==VK_SUCCESS && selected==1);
    }
    assert(ps4AttachmentMemoryTypeBits(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,props)==0);
    assert(ps4AttachmentMemoryTypeBits(VK_IMAGE_USAGE_STORAGE_BIT,props)==0);
    assert(ps4AttachmentMemoryTypeBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,empty)==0);
    assert(ps4AttachmentMemoryTypeBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,cachedOnly)==0);
    auto reordered=props; std::swap(reordered.memoryTypes[0],reordered.memoryTypes[1]);
    assert(ps4AttachmentMemoryTypeBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,reordered)==1);
    auto nonlocal=props; nonlocal.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    assert(ps4AttachmentMemoryTypeBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,nonlocal)==0);
    std::vector<uint32_t> attempts;
    auto run=[&](uint32_t mask,VkResult first,VkResult second) {
        attempts.clear();
        return allocatePs4Attachment(mask,[&](uint32_t bits){
            attempts.push_back(bits);return attempts.size()==1 ? first : second;
        });
    };
    auto success=run(2,VK_SUCCESS,VK_ERROR_DEVICE_LOST);
    assert(success.result==VK_SUCCESS && !success.fallback && attempts==std::vector<uint32_t>{2});
    for(auto error : {VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_FEATURE_NOT_PRESENT}) {
        auto recovered=run(2,error,VK_SUCCESS);
        assert(recovered.result==VK_SUCCESS && recovered.preferredResult==error && recovered.fallback);
        assert((attempts==std::vector<uint32_t>{2,0}));
        auto failed=run(2,error,VK_ERROR_OUT_OF_DEVICE_MEMORY);
        assert(failed.result==VK_ERROR_OUT_OF_DEVICE_MEMORY && failed.fallback);
    }
    auto unavailable=run(0,VK_SUCCESS,VK_ERROR_DEVICE_LOST);
    assert(unavailable.result==VK_SUCCESS && unavailable.fallback && attempts==std::vector<uint32_t>{0});
    for(auto error : {VK_ERROR_DEVICE_LOST,VK_ERROR_INITIALIZATION_FAILED}) {
        auto stopped=run(2,error,VK_SUCCESS);
        assert(stopped.result==error && !stopped.fallback && attempts==std::vector<uint32_t>{2});
    }
    puts("PASS attachment-only selection, actual VMA Garlic selection, reordered/empty/nonlocal types");
    puts("PASS allocation success, recoverable fallback, empty-mask original policy, failed retry, fatal-error no retry");
    vmaDestroyAllocator(allocator); vk_ps4_DestroyInstance(instance,nullptr);
}
