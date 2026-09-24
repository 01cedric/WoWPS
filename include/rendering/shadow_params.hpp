#pragma once

/**
 * Shared shadow descriptor helper for terrain, character and M2 paths:
 * binding 0 is a combined image sampler, binding 1 is a parameter UBO.
 * Terrain still supplies a valid white fallback when it does not sample.
 * WMO shadows instead reuse their main-pass material descriptors through
 * dedicated shaders, preserving cutout alpha without additional ownership.
 */

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace wowee {
namespace rendering {

/// Everything the shadow pass needs bound, and the things behind it that have
/// to be destroyed again.
struct ShadowParamsSet {
    VkBuffer ubo = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    // Persistently mapped together with the allocation. Hot shadow paths update
    // this every frame; retaining the pointer avoids an allocator metadata
    // lookup for every cascade.
    void* mapped = nullptr;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    /// Owned by `pool` - freed with it rather than separately.
    VkDescriptorSet set = VK_NULL_HANDLE;
};

/// Build it. False means one of the five steps failed, and `owner` is the name
/// that appears in the log line saying which.
///
/// `fallbackView` and `fallbackSampler` fill binding 0. A renderer that samples
/// a real texture in its shadow shader rebinds it per draw; one that does not
/// still has to hand Vulkan something.
///
/// Nothing is rolled back on failure: every caller treats a failed shadow
/// setup as "no shadow pass" and goes on to destroy the renderer, which
/// destroys whatever was made.
/// `paramsSize` is the size of the struct behind binding 1. It is not the same
/// for all callers: the character pass has its own shadow shader and its own
/// smaller params - an alpha-test flag and a colour-key flag - where terrain and M2
/// share ShadowParamsUBO. The bindings and everything around them are
/// identical, which is why this is one function and the size is an argument.
bool createShadowParamsSet(VkDevice device, VmaAllocator allocator,
                           VkDeviceSize paramsSize, VkImageView fallbackView,
                           VkSampler fallbackSampler, const char* owner,
                           ShadowParamsSet& out);

/// Take it down, and leave every handle in it null.
///
/// The set is not freed separately: destroying the pool frees the sets
/// allocated from it, and freeing them first would be freeing them twice.
void destroyShadowParamsSet(VkDevice device, VmaAllocator allocator, ShadowParamsSet& s);

}  // namespace rendering
}  // namespace wowee
