#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace wowee::rendering {

// Descriptor storage belongs to a frame slot, not to an individual draw. The
// caller must have completed that slot's fence before beginFrameSlot(). Sets
// remain allocated until their pool is destroyed, eliminating pool teardown
// and direct-memory map/unmap calls from the steady-state shadow pass.
class ShadowTextureCache {
public:
    static constexpr uint32_t FrameCount = 2;
    static constexpr uint32_t Capacity = 256;

    void beginFrameSlot(uint32_t frame) noexcept {
        if (frame < FrameCount) slots_[frame].used = 0;
    }

    // Call only when all owning descriptor pools are being destroyed.
    void forgetPools() noexcept {
        for (auto& frame : slots_) {
            frame.used = 0;
            for (auto& slot : frame.entries) slot = {};
        }
    }

    VkDescriptorSet get(VkDevice device, uint32_t frame, VkDescriptorPool pool,
                        VkDescriptorSetLayout layout, VkBuffer params,
                        VkDeviceSize paramsSize, VkImageView view,
                        VkSampler sampler, VkDescriptorSet fallback) {
        if (frame >= FrameCount || !device || !pool || !layout || !params ||
            !view || !sampler) return fallback;
        auto& f = slots_[frame];
        for (uint32_t i = 0; i < f.used; ++i) {
            const auto& s = f.entries[i];
            if (s.view == view && s.sampler == sampler && s.params == params && s.paramsSize == paramsSize) {
                ++hits_;
                return s.set;
            }
        }
        if (f.used == Capacity) { ++fallbacks_; return fallback; }
        auto& s = f.entries[f.used];
        if (!s.set) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &layout;
            if (vkAllocateDescriptorSets(device, &ai, &s.set) != VK_SUCCESS) {
                s.set = VK_NULL_HANDLE;
                ++fallbacks_;
                return fallback;
            }
            ++allocations_;
        } else {
            ++reuses_;
        }
        // Always refresh on the first use in this frame. Driver object handles
        // can be recycled after a texture dies, so handle equality across
        // frames is NOT proof that a descriptor's backing image is unchanged.
        // Subsequent draws in this frame reuse this immutable set.
        VkDescriptorImageInfo image{sampler, view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo buffer{params, 0, paramsSize};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = s.set;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &image;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = s.set;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].pBufferInfo = &buffer;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        s.view = view;
        s.sampler = sampler;
        s.params = params;
        s.paramsSize = paramsSize;
        ++f.used;
        return s.set;
    }

    uint64_t allocations() const noexcept { return allocations_; }
    uint64_t reuses() const noexcept { return reuses_; }
    uint64_t hits() const noexcept { return hits_; }
    uint64_t fallbacks() const noexcept { return fallbacks_; }

private:
    struct Slot {
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkBuffer params = VK_NULL_HANDLE;
        VkDeviceSize paramsSize = 0;
    };
    struct Frame {
        std::array<Slot, Capacity> entries{};
        uint32_t used = 0;
    };
    std::array<Frame, FrameCount> slots_{};
    uint64_t allocations_ = 0, reuses_ = 0, hits_ = 0, fallbacks_ = 0;
};
} // namespace wowee::rendering
