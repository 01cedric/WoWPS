#include "rendering/skybox.hpp"
#include "rendering/sky_system.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_frame_data.hpp"
#include "core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace wowee {
namespace rendering {

// Push constant struct - must match skybox.frag.glsl layout
struct SkyPushConstants {
    glm::vec4 zenithColor;    // DBC skyTopColor
    glm::vec4 midColor;       // DBC skyMiddleColor
    glm::vec4 horizonColor;   // DBC skyBand1Color
    glm::vec4 fogColor;       // DBC skyBand2Color / fogColor blend
    glm::vec4 sunDirAndTime;  // xyz = sun direction, w = timeOfDay
};
static_assert(sizeof(SkyPushConstants) == 80, "SkyPushConstants size mismatch");

Skybox::Skybox() = default;

Skybox::~Skybox() {
    shutdown();
}

bool Skybox::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    LOG_INFO("Initializing skybox");

    vkCtx = ctx;

    VkDevice device = vkCtx->getDevice();

    // Load SPIR-V shaders
    auto shaders = loadShaderPair(device, "assets/shaders/skybox.vert.spv", "assets/shaders/skybox.frag.spv", "skybox");
    if (!shaders) return false;
    const auto& vertStage = shaders.vertStage;
    const auto& fragStage = shaders.fragStage;

    // Push constant range: 5 x vec4 = 80 bytes
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(SkyPushConstants);  // 80 bytes

    // Create pipeline layout with perFrameLayout (set 0) + push constants
    pipelineLayout = createPipelineLayout(device, {perFrameLayout}, {pushRange});
    if (pipelineLayout == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create skybox pipeline layout");
        return false;
    }

    // Fullscreen triangle - no vertex buffer, no vertex input.
    // Dynamic viewport and scissor
    std::vector<VkDynamicState> dynamicStates = viewportAndScissorDynamic();

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({}, {})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)  // depth test on, write off, LEQUAL for far plane
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device, vkCtx->getPipelineCache());

    // Shader modules can be freed after pipeline creation

    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create skybox pipeline");
        return false;
    }

    LOG_INFO("Skybox initialized");
    return true;
}

void Skybox::recreatePipelines() {
    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();

    destroy(device, pipeline);

    auto shaders = loadShaderPair(device, "assets/shaders/skybox.vert.spv", "assets/shaders/skybox.frag.spv", "skybox");
    if (!shaders) return;
    const auto& vertStage = shaders.vertStage;
    const auto& fragStage = shaders.fragStage;

    std::vector<VkDynamicState> dynamicStates = viewportAndScissorDynamic();

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({}, {})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device, vkCtx->getPipelineCache());


    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Skybox::recreatePipelines: failed to create pipeline");
    }
}

void Skybox::shutdown() {
    if (vkCtx) destroyPipeline(vkCtx->getDevice(), pipeline, pipelineLayout);

    vkCtx = nullptr;
}

void Skybox::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, const SkyParams& params) {
    if (pipeline == VK_NULL_HANDLE || !renderingEnabled) {
        return;
    }

    // Compute sun direction from directionalDir (light points toward scene, sun is opposite)
    glm::vec3 sunDir = -glm::normalize(params.directionalDir);

    SkyPushConstants push{};
    push.zenithColor   = glm::vec4(params.skyTopColor, 1.0f);
    push.midColor      = glm::vec4(params.skyMiddleColor, 1.0f);
    push.horizonColor  = glm::vec4(params.skyBand1Color, 1.0f);
    push.fogColor      = glm::vec4(params.skyBand2Color, 1.0f);
    push.sunDirAndTime = glm::vec4(sunDir, params.timeOfDay);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind per-frame descriptor set (set 0 - camera UBO)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        0, 1, &perFrameSet, 0, nullptr);

    // Push constants
    vkCmdPushConstants(cmd, pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(push), &push);

    // Draw fullscreen triangle - no vertex buffer needed
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Skybox::update(float deltaTime) {
    if (timeProgressionEnabled) {
        timeOfDay += deltaTime * timeSpeed;

        // Wrap around 24 hours
        if (timeOfDay >= 24.0f) {
            timeOfDay -= 24.0f;
        }
    }
}

void Skybox::setTimeOfDay(float time) {
    // Wrap to [0, 24) range using fmod instead of iterative subtraction
    time = std::fmod(time, 24.0f);
    if (time < 0.0f) time += 24.0f;
    timeOfDay = time;
}

} // namespace rendering
} // namespace wowee
