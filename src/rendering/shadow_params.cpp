#include "rendering/shadow_params.hpp"

#include <cstring>

#include "core/logger.hpp"
#include "rendering/vk_utils.hpp"

namespace wowee {
namespace rendering {

bool createShadowParamsSet(VkDevice device, VmaAllocator allocator,
                           VkDeviceSize paramsSize, VkImageView fallbackView,
                           VkSampler fallbackSampler, const char* owner,
                           ShadowParamsSet& out) {
    // The uniform buffer, mapped for the whole of its life: the shadow pass
    // rewrites it per draw, so it is written far too often to be worth staging.
    VkBufferCreateInfo bufCI{};
    bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCI.size = paramsSize;
    bufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocInfo{};
    if (vmaCreateBuffer(allocator, &bufCI, &allocCI, &out.ubo, &out.alloc, &allocInfo) !=
        VK_SUCCESS) {
        LOG_ERROR(owner, ": failed to create shadow params UBO");
        return false;
    }
    // Zeroed rather than left as whatever the allocator handed back - a draw
    // that reaches the shader before the first per-draw write would otherwise
    // read a random alpha cutoff and a random useTexture.
    std::memset(allocInfo.pMappedData, 0, static_cast<size_t>(paramsSize));

    // Binding 0 is the texture, binding 1 the params.
    //
    // The params are visible to the vertex stage as well as the fragment one.
    // Three of the four callers wrote it that way and the character pass did
    // not, whose shader reads them only in the fragment stage - visibility a
    // shader does not use costs nothing and is legal, where the reverse is
    // neither, so the wider of the two is the one to share.
    VkDescriptorSetLayoutBinding layoutBindings[2]{};
    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = 2;
    layoutCI.pBindings = layoutBindings;
    if (vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &out.layout) != VK_SUCCESS) {
        LOG_ERROR(owner, ": failed to create shadow params set layout");
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 2;
    poolCI.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolCI, nullptr, &out.pool) != VK_SUCCESS) {
        LOG_ERROR(owner, ": failed to create shadow params pool");
        return false;
    }

    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = out.pool;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &out.layout;
    if (vkAllocateDescriptorSets(device, &setAlloc, &out.set) != VK_SUCCESS) {
        LOG_ERROR(owner, ": failed to allocate shadow params set");
        return false;
    }

    // The white fallback for binding 0. Terrain and WMOs never sample it -
    // useTexture is 0 in their params - but a set with an unwritten binding is
    // not a set Vulkan will let anything bind.
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = out.ubo;
    bufInfo.offset = 0;
    bufInfo.range = paramsSize;
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView = fallbackView;
    imgInfo.sampler = fallbackSampler;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = out.set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &imgInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = out.set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    return true;
}

void destroyShadowParamsSet(VkDevice device, VmaAllocator allocator, ShadowParamsSet& s) {
    if (s.pool) {
        vkDestroyDescriptorPool(device, s.pool, nullptr);
        s.pool = VK_NULL_HANDLE;
        // Freed with the pool, so this handle is only ever stale after it.
        s.set = VK_NULL_HANDLE;
    }
    destroy(device, s.layout);
    destroy(allocator, s.ubo, s.alloc);
}

}  // namespace rendering
}  // namespace wowee
