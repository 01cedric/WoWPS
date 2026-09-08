#include "rendering/bone_slots.hpp"

#include "rendering/vk_context.hpp"

namespace wowee {
namespace rendering {

void releaseBoneSlot(VkContext& ctx, VkDescriptorPool pool,
                     const std::shared_ptr<std::atomic<uint64_t>>& poolGeneration,
                     VkDescriptorSet set, VkBuffer buffer, VmaAllocation allocation,
                     bool defer) {
    if (set == VK_NULL_HANDLE && buffer == VK_NULL_HANDLE) return;

    VkDevice device = ctx.getDevice();
    VmaAllocator alloc = ctx.getAllocator();

    if (!defer) {
        // Immediate, which is only safe once the device is idle.
        if (set != VK_NULL_HANDLE && pool != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(device, pool, 1, &set);
        }
        if (buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(alloc, buffer, allocation);
        }
        return;
    }

    const uint64_t generation =
        poolGeneration ? poolGeneration->load(std::memory_order_relaxed) : 0;
    ctx.deferAfterAllFrameFences(
        [device, alloc, pool, poolGeneration, generation, set, buffer, allocation]() {
            const bool poolStillValid =
                poolGeneration &&
                poolGeneration->load(std::memory_order_relaxed) == generation;
            if (set != VK_NULL_HANDLE && pool != VK_NULL_HANDLE && poolStillValid) {
                VkDescriptorSet s = set;
                vkFreeDescriptorSets(device, pool, 1, &s);
            }
            if (buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(alloc, buffer, allocation);
            }
        });
}

}  // namespace rendering
}  // namespace wowee
