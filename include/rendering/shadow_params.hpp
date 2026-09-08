#pragma once

/**
 * shadow_params.hpp - the descriptor set every shadow pass binds.
 *
 * Four renderers draw into the shadow map - terrain, WMOs, characters and M2s
 * - and each one needs the same set before it can: binding 0 a combined image
 * sampler, binding 1 a small uniform buffer of ShadowParams. Terrain and WMOs
 * never sample that texture (their shader has useTexture = 0), but the binding
 * has to be filled with something valid, so all four point it at a white
 * fallback.
 *
 * All four wrote that out for themselves - the buffer, the layout, the pool,
 * the set and the two writes, some eighty lines apiece - and the copies had
 * begun to drift: two spell the failure through LOG_ERROR and one through the
 * logger directly, three clear the set handle after destroying the pool that
 * owns it and one does not. None of that is a fault today, because the handles
 * that gate use are nulled everywhere. It is four chances for the next change
 * to land in three places.
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
/// for all four: the character pass has its own shadow shader and its own
/// smaller params - an alpha-test flag and a colour-key flag - where the other
/// three share ShadowParamsUBO. The bindings and everything around them are
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
