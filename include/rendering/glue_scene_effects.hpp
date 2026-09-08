#pragma once

#include "rendering/character_renderer.hpp"
#include <memory>

namespace wowee::rendering {

// Authored M2 menu emitters, sharing the scene's animation clock and bones.
// The mesh remains owned by CharacterRenderer. GPU data has two fenced slots.
class GlueSceneEffects {
public:
    GlueSceneEffects();
    ~GlueSceneEffects();
    GlueSceneEffects(const GlueSceneEffects&) = delete;
    GlueSceneEffects& operator=(const GlueSceneEffects&) = delete;

    bool load(const pipeline::M2Model& scene, VkContext* context,
              VkDescriptorSetLayout perFrameLayout, pipeline::AssetManager* assets,
              VkRenderPass renderPass,
              VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    void update(float deltaTime, const CharacterRenderer::SceneFrame& frame);
    // Call inside the scene pass, after its mesh, using the same camera and UBO.
    // No viewport/scissor changes: the caller owns the preview viewport.
    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet,
                const Camera& camera, uint32_t frameIndex);
    void clear();
    [[nodiscard]] bool loaded() const;
    [[nodiscard]] size_t particleCount() const;
    [[nodiscard]] size_t ribbonEdgeCount() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace wowee::rendering
