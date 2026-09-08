#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace wowee {
namespace rendering {

class VkContext;

/// Releases one frame slot's bone descriptor set and its uniform buffer.
///
/// Every skinned instance carries two of these, one per frame in flight, and
/// two renderers destroy them: the character renderer for units and the M2
/// renderer for doodads and spell effects. Both wrote the same forty lines,
/// and the copies had drifted in two ways that only show up under a validation
/// layer or a device loss, so they are worth having in one place.
///
/// `defer` is what makes this awkward enough to get wrong. Destroying an
/// instance during the frame loop touches BOTH frame slots, and the slot that
/// is not the current one may still have a command buffer in flight, so the
/// work has to wait on every frame fence rather than the current frame's. The
/// descriptor pool can also be rebuilt while that wait is outstanding, which
/// is what the generation counter is for: freeing a set back into a pool that
/// has since been replaced corrupts the new pool.
///
/// The caller clears its own handles only after this succeeds; these are copies.
void releaseBoneSlot(VkContext& ctx, VkDescriptorPool pool,
                     const std::shared_ptr<std::atomic<uint64_t>>& poolGeneration,
                     VkDescriptorSet set, VkBuffer buffer, VmaAllocation allocation,
                     bool defer);

/// Releases both frame slots of one skinned instance.
///
/// The loop around releaseBoneSlot was written out twice as well - once per
/// renderer, over two instance types whose bone members are named alike. What
/// the copies shared was the ordering the note above depends on: take the
/// handles, enqueue their release, and only then blank the slot. A failed
/// enqueue must leave ownership with the instance. A template keeps that order
/// in one place; the two instance types differ in everything else.
template <typename Instance>
void releaseInstanceBones(VkContext& ctx, VkDescriptorPool pool,
                          const std::shared_ptr<std::atomic<uint64_t>>& poolGeneration,
                          Instance& inst, bool defer) {
    for (int i = 0; i < 2; i++) {
        // Allocation failure while enqueueing retains the owning handles.
        const VkDescriptorSet boneSet = inst.boneSet[i];
        const ::VkBuffer boneBuf = inst.boneBuffer[i];
        const VmaAllocation boneAlloc = inst.boneAlloc[i];
        releaseBoneSlot(ctx, pool, poolGeneration, boneSet, boneBuf, boneAlloc, defer);
        inst.boneSet[i] = VK_NULL_HANDLE;
        inst.boneBuffer[i] = VK_NULL_HANDLE;
        inst.boneAlloc[i] = VK_NULL_HANDLE;
        inst.boneMapped[i] = nullptr;
    }
}

}  // namespace rendering
}  // namespace wowee
