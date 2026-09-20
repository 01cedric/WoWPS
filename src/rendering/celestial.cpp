#include "rendering/celestial.hpp"
#include "rendering/celestial_lighting.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_utils.hpp"
#include "core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>

namespace wowee {
namespace rendering {

Celestial::Celestial() = default;

Celestial::~Celestial() {
    shutdown();
}

VkPipeline Celestial::buildPipeline(VkDevice device,
                                    const VkPipelineShaderStageCreateInfo& vertStage,
                                    const VkPipelineShaderStageCreateInfo& fragStage) {
    // Vertex: vec3 pos + vec2 texCoord, stride = 20 bytes
    VkVertexInputBindingDescription binding = tightVertexBinding(5 * sizeof(float));
    std::vector<VkVertexInputAttributeDescription> attrs = positionPlusUvAttrs();
    std::vector<VkDynamicState> dynamicStates = viewportAndScissorDynamic();

    return PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, attrs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setNoDepthTest() // Sky layer: celestials always render (skybox doesn't write depth)
        .setColorBlendAttachment(PipelineBuilder::blendAdditive())
        .setMultisample(vkCtx_->getMsaaSamples())
        .setLayout(pipelineLayout_)
        .setRenderPass(vkCtx_->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device, vkCtx_->getPipelineCache());
}

bool Celestial::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    LOG_INFO("Initializing celestial renderer (Vulkan)");

    vkCtx_ = ctx;
    VkDevice device = vkCtx_->getDevice();

    // ------------------------------------------------------------------ shaders
    auto shaders = loadShaderPair(device, "assets/shaders/celestial.vert.spv", "assets/shaders/celestial.frag.spv", "celestial");
    if (!shaders) return false;
    const auto& vertStage = shaders.vertStage;
    const auto& fragStage = shaders.fragStage;

    // ------------------------------------------------------------------ push constants
    // Layout: mat4(64) + vec4(16) + float*3(12) + pad(4) = 96 bytes
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(CelestialPush); // 96 bytes

    // ------------------------------------------------------------------ pipeline layout
    pipelineLayout_ = createPipelineLayout(device, {perFrameLayout}, {pushRange});
    if (pipelineLayout_ == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create celestial pipeline layout");
        return false;
    }

    // ------------------------------------------------------------------ pipeline
    pipeline_ = buildPipeline(device, vertStage, fragStage);


    if (pipeline_ == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create celestial pipeline");
        return false;
    }

    // ------------------------------------------------------------------ geometry
    createQuad();

    LOG_INFO("Celestial renderer initialized");
    return true;
}

void Celestial::recreatePipelines() {
    if (!vkCtx_) return;
    VkDevice device = vkCtx_->getDevice();

    destroy(device, pipeline_);

    auto shaders = loadShaderPair(device, "assets/shaders/celestial.vert.spv", "assets/shaders/celestial.frag.spv", "celestial");
    if (!shaders) return;
    const auto& vertStage = shaders.vertStage;
    const auto& fragStage = shaders.fragStage;

    pipeline_ = buildPipeline(device, vertStage, fragStage);


    if (pipeline_ == VK_NULL_HANDLE) {
        LOG_ERROR("Celestial::recreatePipelines: failed to create pipeline");
    }
}

void Celestial::shutdown() {
    destroyQuad();

    if (vkCtx_) destroyPipeline(vkCtx_->getDevice(), pipeline_, pipelineLayout_);

    vkCtx_ = nullptr;
}

// ---------------------------------------------------------------------------
// Public render entry point
// ---------------------------------------------------------------------------

void Celestial::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet,
                       float timeOfDay,
                       const glm::vec3* sunDir, const glm::vec3* sunColor,
                       float gameTime, float nightFactor) {
    if (!renderingEnabled_ || pipeline_ == VK_NULL_HANDLE) {
        return;
    }

    // This input is hours since midnight, not elapsed seconds or a date.
    // Decorative phase cycling runs once in update(), never once per view.
    (void)gameTime;

    // Bind pipeline and per-frame descriptor set once - reused for all draws
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
        0, 1, &perFrameSet, 0, nullptr);

    // Bind the shared quad buffers
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    // Draw sun, then moon(s) - each call pushes different constants
    renderSun(cmd, perFrameSet, timeOfDay, sunDir, sunColor);
    renderMoon(cmd, perFrameSet, timeOfDay, nightFactor, sunDir, sunColor);
    if (dualMoonMode_) {
        renderBlueChild(cmd, perFrameSet, timeOfDay, nightFactor, sunDir, sunColor);
    }
}

// ---------------------------------------------------------------------------
// Private per-body render helpers
// ---------------------------------------------------------------------------

void Celestial::renderSun(VkCommandBuffer cmd, VkDescriptorSet /*perFrameSet*/,
                           float timeOfDay,
                           const glm::vec3* sunDir, const glm::vec3* sunColor) {
    const glm::vec3 lightDir = celestialSolarTravelDirection(
        sunDir ? *sunDir : glm::vec3(0.0f), timeOfDay);
    const glm::vec3 dir = -lightDir;
    if (dir.z <= 0.0f) return;

    const float sunDistance = 800.0f;
    glm::vec3 sunPos = dir * sunDistance;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, sunPos);
    model = glm::scale(model, glm::vec3(95.0f, 95.0f, 1.0f));

    const glm::vec3 color = celestialDiscColor(sunColor ? *sunColor : getSunColor(timeOfDay));
    const float intensity = celestialDiscStrength(lightDir, false) * 0.92f;

    CelestialPush push{};
    push.model          = model;
    push.celestialColor = glm::vec4(color, 1.0f);
    push.intensity      = intensity;
    push.moonPhase      = 0.5f; // unused for sun
    push.animTime       = sunHazeTimer_;

    vkCmdPushConstants(cmd, pipelineLayout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(push), &push);

    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
}

void Celestial::renderMoon(VkCommandBuffer cmd, VkDescriptorSet /*perFrameSet*/,
                            float timeOfDay, float nightFactor, const glm::vec3* sunDir, const glm::vec3* sunColor) {
    // The primary moon is opposite the same resolved sun vector used for
    // world lighting. A separately animated x/z orbit previously disagreed
    // with every tree shadow and every volumetric ray at night.
    const glm::vec3 moonDir = celestialSolarTravelDirection(
        sunDir ? *sunDir : glm::vec3(0.0f), timeOfDay);
    if (nightFactor < 0.01f || moonDir.z <= 0.0f) return;
    const glm::vec3 moonPos = moonDir * 800.0f;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, moonPos);
    model = glm::scale(model, glm::vec3(40.0f, 40.0f, 1.0f));

    glm::vec3 color = celestialDiscColor(sunColor ? *sunColor : glm::vec3(0.8f, 0.85f, 1.0f));

    float intensity = glm::clamp(nightFactor, 0.0f, 1.0f) * celestialDiscStrength(moonDir, true);

    CelestialPush push{};
    push.model          = model;
    push.celestialColor = glm::vec4(color, 0.0f);  // w=0 marks moon for the shader
    push.intensity      = intensity;
    push.moonPhase      = whiteLadyPhase_;
    push.animTime       = sunHazeTimer_;

    vkCmdPushConstants(cmd, pipelineLayout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(push), &push);

    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
}

void Celestial::renderBlueChild(VkCommandBuffer cmd, VkDescriptorSet /*perFrameSet*/,
                                 float timeOfDay, float nightFactor, const glm::vec3* sunDir, const glm::vec3* sunColor) {
    const glm::vec3 moonDir = celestialSolarTravelDirection(
        sunDir ? *sunDir : glm::vec3(0.0f), timeOfDay);
    if (nightFactor < 0.01f || moonDir.z <= 0.0f) return;
    // The secondary moon remains decorative; one shadow map uses White Lady.
    glm::vec3 moonPos = moonDir * 800.0f;
    moonPos.x += 80.0f;
    moonPos.z -= 40.0f;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, moonPos);
    model = glm::scale(model, glm::vec3(30.0f, 30.0f, 1.0f));

    glm::vec3 color = celestialDiscColor(sunColor ? *sunColor : glm::vec3(0.7f, 0.8f, 1.0f));

    float intensity = 0.7f * glm::clamp(nightFactor, 0.0f, 1.0f) * celestialDiscStrength(moonDir, true);

    CelestialPush push{};
    push.model          = model;
    push.celestialColor = glm::vec4(color, 0.0f);  // w=0 marks moon for the shader
    push.intensity      = intensity;
    push.moonPhase      = blueChildPhase_;
    push.animTime       = sunHazeTimer_;

    vkCmdPushConstants(cmd, pipelineLayout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(push), &push);

    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Position / colour query helpers (identical logic to GL version)
// ---------------------------------------------------------------------------

glm::vec3 Celestial::getSunPosition(float timeOfDay) const {
    return -sunTravelDirection(timeOfDay / 24.0f) * 800.0f;
}

glm::vec3 Celestial::getMoonPosition(float timeOfDay) const {
    return sunTravelDirection(timeOfDay / 24.0f) * 800.0f;
}

glm::vec3 Celestial::getSunColor(float timeOfDay) const {
    if (timeOfDay >= 5.0f && timeOfDay < 7.0f) {
        return glm::vec3(1.0f, 0.6f, 0.2f); // Sunrise orange
    }
    if (timeOfDay >= 7.0f && timeOfDay < 9.0f) {
        float t = (timeOfDay - 7.0f) / 2.0f;
        return glm::mix(glm::vec3(1.0f, 0.6f, 0.2f), glm::vec3(1.0f, 1.0f, 0.9f), t);
    }
    if (timeOfDay >= 9.0f && timeOfDay < 16.0f) {
        return glm::vec3(1.0f, 1.0f, 0.9f); // Day yellow-white
    }
    if (timeOfDay >= 16.0f && timeOfDay < 18.0f) {
        float t = (timeOfDay - 16.0f) / 2.0f;
        return glm::mix(glm::vec3(1.0f, 1.0f, 0.9f), glm::vec3(1.0f, 0.5f, 0.1f), t);
    }
    return glm::vec3(1.0f, 0.4f, 0.1f); // Sunset orange
}

float Celestial::getSunIntensity(float timeOfDay) const {
    return celestialDiscStrength(sunTravelDirection(timeOfDay / 24.0f), false);
}

// ---------------------------------------------------------------------------
// Moon phase helpers
// ---------------------------------------------------------------------------

void Celestial::update(float deltaTime) {
    sunHazeTimer_ += deltaTime;
    // Keep timer in a range where GPU sin() precision is reliable (< ~10000).
    // The noise period repeats at multiples of 1.0 on each axis, so fmod by a
    // large integer preserves visual continuity.
    if (sunHazeTimer_ > 10000.0f) {
        sunHazeTimer_ = std::fmod(sunHazeTimer_, 10000.0f);
    }

    if (!moonPhaseCycling_) {
        return;
    }

    moonPhaseTimer_ += std::max(deltaTime, 0.0f);
    whiteLadyPhase_ = animatedMoonPhase(0.5f, moonPhaseTimer_, MOON_CYCLE_DURATION);

    constexpr float BLUE_CHILD_CYCLE = 210.0f; // Slightly faster: 3.5 minutes
    blueChildPhase_ = animatedMoonPhase(0.25f, moonPhaseTimer_, BLUE_CHILD_CYCLE);
}
void Celestial::setBlueChildPhase(float phase) {
    blueChildPhase_ = glm::clamp(phase, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// GPU buffer management
// ---------------------------------------------------------------------------

void Celestial::createQuad() {
    // Billboard quad centred at origin, vertices: pos(vec3) + uv(vec2)
    float vertices[] = {
        // Position              TexCoord
        -0.5f,  0.5f, 0.0f,    0.0f, 1.0f, // Top-left
         0.5f,  0.5f, 0.0f,    1.0f, 1.0f, // Top-right
         0.5f, -0.5f, 0.0f,    1.0f, 0.0f, // Bottom-right
        -0.5f, -0.5f, 0.0f,    0.0f, 0.0f, // Bottom-left
    };

    uint32_t indices[] = { 0, 1, 2,  0, 2, 3 };

    AllocatedBuffer vbuf = uploadBuffer(*vkCtx_,
        vertices, sizeof(vertices),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    vertexBuffer_ = vbuf.buffer;
    vertexAlloc_  = vbuf.allocation;

    AllocatedBuffer ibuf = uploadBuffer(*vkCtx_,
        indices, sizeof(indices),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    indexBuffer_ = ibuf.buffer;
    indexAlloc_  = ibuf.allocation;
}

void Celestial::destroyQuad() {
    if (!vkCtx_) return;

    VmaAllocator allocator = vkCtx_->getAllocator();

    destroy(allocator, vertexBuffer_, vertexAlloc_);
    destroy(allocator, indexBuffer_, indexAlloc_);
}

} // namespace rendering
} // namespace wowee
