#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <vector>
#include <unordered_map>

#include "rendering/vk_texture.hpp"

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering {

class Camera;
class VkContext;

/**
 * Renders the client's original quest icons as billboards above NPC heads.
 * Artwork comes from Interface/GossipFrame in the player's asset archive.
 */
class QuestMarkerRenderer {
public:
    QuestMarkerRenderer();
    ~QuestMarkerRenderer();

    [[nodiscard]] bool initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout, pipeline::AssetManager* assetManager);
    void shutdown();
    void recreatePipelines();

    /**
     * Add or update a quest marker at a position
     * @param guid NPC GUID
     * @param position World position (NPC base position)
     * @param markerType 0=available(!), 1=turnin(?), 2=incomplete(?), -1=remove
     * @param boundingHeight NPC bounding height (optional, default 2.0f)
     * @param grayscale 0 = colour, 1 = grey (trivial quests); incomplete is always grey
     */
    void setMarker(uint64_t guid, const glm::vec3& position, int markerType,
                   float boundingHeight = 2.0f, float grayscale = 0.0f);


    /**
     * Clear all markers
     */
    void clear();

    /**
     * Render all quest markers (call after world rendering, before UI)
     * @param cmd Command buffer to record into
     * @param perFrameSet Per-frame descriptor set (set 0, contains camera UBO)
     * @param camera Camera for billboard calculation (CPU-side view matrix)
     */
    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, const Camera& camera);

private:
    struct Marker {
        glm::vec3 position;
        int type; // 0=available, 1=turnin, 2=incomplete
        float boundingHeight = 2.0f;
        float grayscale = 0.0f; // 0 = colour, 1 = desaturated (trivial quests)
    };

    std::unordered_map<uint64_t, Marker> markers_;

    // Vulkan context
    VkContext* vkCtx_ = nullptr;

    // Pipeline
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    // Descriptor resources for per-material texture (set 1)
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet texDescSets_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};

    // Textures: available, turnin, incomplete
    VkTexture textures_[3];

    // Quad vertex buffer
    VkBuffer quadVB_ = VK_NULL_HANDLE;
    VmaAllocation quadVBAlloc_ = VK_NULL_HANDLE;

    bool createQuad();
    bool loadTextures(pipeline::AssetManager* assetManager);
    bool createDescriptorResources();
};

} // namespace rendering
} // namespace wowee
