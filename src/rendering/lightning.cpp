#include "rendering/lightning.hpp"
#include "core/coordinates.hpp"
#include "rendering/camera.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_utils.hpp"
#include "core/logger.hpp"
#include <random>
#include <cmath>
#include <cstring>

namespace wowee {
namespace rendering {

namespace {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float randomRange(float min, float max) {
        return min + dist(gen) * (max - min);
    }
}

Lightning::Lightning() {
    flash.active = false;
    flash.intensity = 0.0f;
    flash.lifetime = 0.0f;
    flash.maxLifetime = FLASH_LIFETIME;

    bolts.resize(MAX_BOLTS);
    for (auto& bolt : bolts) {
        bolt.active = false;
        bolt.lifetime = 0.0f;
        bolt.maxLifetime = BOLT_LIFETIME;
        bolt.brightness = 1.0f;
    }

    // Random initial strike time
    nextStrikeTime = randomRange(MIN_STRIKE_INTERVAL, MAX_STRIKE_INTERVAL);
}

Lightning::~Lightning() {
    shutdown();
}

/// The bolt pipeline: line-list geometry, additive, no depth test.
///
/// initialize() and recreatePipelines() described this and the flash pipeline
/// below identically, vertex layout included, so each was stated twice.
void Lightning::buildBoltPipeline(VkDevice device,
                                  const VkPipelineShaderStageCreateInfo& vertStage,
                                  const VkPipelineShaderStageCreateInfo& fragStage) {
    const std::vector<VkDynamicState> dynamicStates = viewportAndScissorDynamic();

    // Vertex input: position only (vec3)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(glm::vec3);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttr.offset = 0;

    boltPipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, {posAttr})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setNoDepthTest()  // Always visible (like the GL version)
        .setColorBlendAttachment(PipelineBuilder::blendAdditive())  // Additive for electric glow
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(boltPipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device, vkCtx->getPipelineCache());
}

/// The flash pipeline: a fullscreen quad tinting the sky when a bolt strikes.
void Lightning::buildFlashPipeline(VkDevice device,
                                   const VkPipelineShaderStageCreateInfo& vertStage,
                                   const VkPipelineShaderStageCreateInfo& fragStage) {
    const std::vector<VkDynamicState> dynamicStates = viewportAndScissorDynamic();

    // Vertex input: position only (vec2)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = VK_FORMAT_R32G32_SFLOAT;
    posAttr.offset = 0;

    flashPipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, {posAttr})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setNoDepthTest()
        .setColorBlendAttachment(PipelineBuilder::blendAlpha())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(flashPipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device, vkCtx->getPipelineCache());
}

bool Lightning::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    core::Logger::getInstance().info("Initializing lightning system...");

    vkCtx = ctx;
    VkDevice device = vkCtx->getDevice();


    // ---- Bolt pipeline (LINE_STRIP) ----
    {
        auto shaders = loadShaderPair(device, "assets/shaders/lightning_bolt.vert.spv", "assets/shaders/lightning_bolt.frag.spv", "lightning_bolt");
        if (!shaders) return false;
        const auto& vertStage = shaders.vertStage;
        const auto& fragStage = shaders.fragStage;

        // Push constant: { float brightness; } = 4 bytes
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float);

        boltPipelineLayout = createPipelineLayout(device, {perFrameLayout}, {pushRange});
        if (boltPipelineLayout == VK_NULL_HANDLE) {
            core::Logger::getInstance().error("Failed to create bolt pipeline layout");
            return false;
        }

        buildBoltPipeline(device, vertStage, fragStage);


        if (boltPipeline == VK_NULL_HANDLE) {
            core::Logger::getInstance().error("Failed to create bolt pipeline");
            return false;
        }
    }

    // ---- Flash pipeline (fullscreen quad, TRIANGLE_STRIP) ----
    {
        auto shaders = loadShaderPair(device, "assets/shaders/lightning_flash.vert.spv", "assets/shaders/lightning_flash.frag.spv", "lightning_flash");
        if (!shaders) return false;
        const auto& vertStage = shaders.vertStage;
        const auto& fragStage = shaders.fragStage;

        // Push constant: { float intensity; } = 4 bytes
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float);

        flashPipelineLayout = createPipelineLayout(device, {}, {pushRange});
        if (flashPipelineLayout == VK_NULL_HANDLE) {
            core::Logger::getInstance().error("Failed to create flash pipeline layout");
            return false;
        }

        buildFlashPipeline(device, vertStage, fragStage);


        if (flashPipeline == VK_NULL_HANDLE) {
            core::Logger::getInstance().error("Failed to create flash pipeline");
            return false;
        }
    }

    // ---- Create dynamic mapped vertex buffer for bolt segments ----
    // Each bolt can have up to MAX_SEGMENTS * 2 vec3 entries (segments + branches)
    boltDynamicVBSize = MAX_SEGMENTS * 4 * sizeof(glm::vec3);  // generous capacity
    {
        AllocatedBuffer buf = createBuffer(vkCtx->getAllocator(), boltDynamicVBSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        boltDynamicVB = buf.buffer;
        boltDynamicVBAlloc = buf.allocation;
        boltDynamicVBAllocInfo = buf.info;
        if (boltDynamicVB == VK_NULL_HANDLE) {
            core::Logger::getInstance().error("Failed to create bolt dynamic vertex buffer");
            return false;
        }
    }

    // ---- Create static flash quad vertex buffer ----
    {
        float flashQuad[] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
            -1.0f,  1.0f,
             1.0f,  1.0f
        };

        AllocatedBuffer buf = uploadBuffer(*vkCtx, flashQuad, sizeof(flashQuad),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        flashQuadVB = buf.buffer;
        flashQuadVBAlloc = buf.allocation;
        if (flashQuadVB == VK_NULL_HANDLE) {
            core::Logger::getInstance().error("Failed to create flash quad vertex buffer");
            return false;
        }
    }

    core::Logger::getInstance().info("Lightning system initialized");
    return true;
}

void Lightning::shutdown() {
    if (vkCtx) {
        VkDevice device = vkCtx->getDevice();
        VmaAllocator allocator = vkCtx->getAllocator();

        destroy(device, boltPipeline);
        destroy(device, boltPipelineLayout);
        destroy(allocator, boltDynamicVB, boltDynamicVBAlloc);

        destroy(device, flashPipeline);
        destroy(device, flashPipelineLayout);
        destroy(allocator, flashQuadVB, flashQuadVBAlloc);
    }

    vkCtx = nullptr;
}

void Lightning::recreatePipelines() {
    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();

    // Destroy old pipelines (NOT layouts)
    destroy(device, boltPipeline);
    destroy(device, flashPipeline);


    // ---- Rebuild bolt pipeline (LINE_STRIP) ----
    {
        auto shaders = loadShaderPair(device, "assets/shaders/lightning_bolt.vert.spv", "assets/shaders/lightning_bolt.frag.spv", "lightning_bolt");
        if (!shaders) return;
        const auto& vertStage = shaders.vertStage;
        const auto& fragStage = shaders.fragStage;

        buildBoltPipeline(device, vertStage, fragStage);

    }

    // ---- Rebuild flash pipeline (TRIANGLE_STRIP) ----
    {
        auto shaders = loadShaderPair(device, "assets/shaders/lightning_flash.vert.spv", "assets/shaders/lightning_flash.frag.spv", "lightning_flash");
        if (!shaders) return;
        const auto& vertStage = shaders.vertStage;
        const auto& fragStage = shaders.fragStage;

        buildFlashPipeline(device, vertStage, fragStage);

    }
}

void Lightning::update(float deltaTime, const Camera& camera) {
    if (!enabled) {
        return;
    }

    // Update strike timer
    strikeTimer += deltaTime;

    // Spawn random strikes based on intensity
    if (strikeTimer >= nextStrikeTime) {
        spawnRandomStrike(camera.getPosition());
        strikeTimer = 0.0f;

        // Calculate next strike time (higher intensity = more frequent)
        float intervalRange = MAX_STRIKE_INTERVAL - MIN_STRIKE_INTERVAL;
        float adjustedInterval = MIN_STRIKE_INTERVAL + intervalRange * (1.0f - intensity);
        nextStrikeTime = randomRange(adjustedInterval * 0.8f, adjustedInterval * 1.2f);
    }

    updateBolts(deltaTime);
    updateFlash(deltaTime);
}

void Lightning::updateBolts(float deltaTime) {
    for (auto& bolt : bolts) {
        if (!bolt.active) {
            continue;
        }

        bolt.lifetime += deltaTime;
        if (bolt.lifetime >= bolt.maxLifetime) {
            bolt.active = false;
            continue;
        }

        // Fade out
        float t = bolt.lifetime / bolt.maxLifetime;
        bolt.brightness = 1.0f - t;
    }
}

void Lightning::updateFlash(float deltaTime) {
    if (!flash.active) {
        return;
    }

    flash.lifetime += deltaTime;
    if (flash.lifetime >= flash.maxLifetime) {
        flash.active = false;
        flash.intensity = 0.0f;
        return;
    }

    // Quick fade
    float t = flash.lifetime / flash.maxLifetime;
    flash.intensity = 1.0f - (t * t);  // Quadratic fade
}

void Lightning::spawnRandomStrike(const glm::vec3& cameraPos) {
    // Find inactive bolt
    LightningBolt* bolt = nullptr;
    for (auto& b : bolts) {
        if (!b.active) {
            bolt = &b;
            break;
        }
    }

    if (!bolt) {
        return;  // All bolts active
    }

    // Random position around camera
    float angle = randomRange(0.0f, core::coords::TWO_PI);
    float distance = randomRange(50.0f, STRIKE_DISTANCE);

    glm::vec3 strikePos;
    strikePos.x = cameraPos.x + std::cos(angle) * distance;
    strikePos.z = cameraPos.z + std::sin(angle) * distance;
    strikePos.y = cameraPos.y + randomRange(80.0f, 150.0f);  // High in sky

    triggerStrike(strikePos);
}

void Lightning::triggerStrike(const glm::vec3& position) {
    // Find inactive bolt
    LightningBolt* bolt = nullptr;
    for (auto& b : bolts) {
        if (!b.active) {
            bolt = &b;
            break;
        }
    }

    if (!bolt) {
        return;
    }

    // Setup bolt
    bolt->active = true;
    bolt->lifetime = 0.0f;
    bolt->brightness = 1.0f;
    bolt->startPos = position;
    bolt->endPos = position;
    bolt->endPos.y = position.y - randomRange(100.0f, 200.0f);  // Strike downward

    // Generate segments
    bolt->segments.clear();
    bolt->branches.clear();
    generateLightningBolt(*bolt);

    // Trigger screen flash
    flash.active = true;
    flash.lifetime = 0.0f;
    flash.intensity = 1.0f;
}

void Lightning::generateLightningBolt(LightningBolt& bolt) {
    generateBoltSegments(bolt.startPos, bolt.endPos, bolt.segments, 0);
}

void Lightning::generateBoltSegments(const glm::vec3& start, const glm::vec3& end,
                                     std::vector<glm::vec3>& segments, int depth) {
    if (depth > 4) {  // Max recursion depth
        return;
    }

    int numSegments = 8 + static_cast<int>(randomRange(0.0f, 8.0f));
    glm::vec3 direction = end - start;
    float length = glm::length(direction);
    direction = glm::normalize(direction);

    glm::vec3 current = start;
    segments.push_back(current);

    for (int i = 1; i < numSegments; i++) {
        float t = static_cast<float>(i) / static_cast<float>(numSegments);
        glm::vec3 target = start + direction * (length * t);

        // Add random offset perpendicular to direction
        float offsetAmount = (1.0f - t) * 8.0f;  // More offset at start
        glm::vec3 perpendicular1 = glm::normalize(glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 perpendicular2 = glm::normalize(glm::cross(direction, perpendicular1));

        glm::vec3 offset = perpendicular1 * randomRange(-offsetAmount, offsetAmount) +
                          perpendicular2 * randomRange(-offsetAmount, offsetAmount);

        current = target + offset;
        segments.push_back(current);

        // Random branches
        if (dist(gen) < BRANCH_PROBABILITY && depth < 3) {
            glm::vec3 branchEnd = current;
            branchEnd += glm::vec3(randomRange(-20.0f, 20.0f),
                                   randomRange(-30.0f, -10.0f),
                                   randomRange(-20.0f, 20.0f));
            generateBoltSegments(current, branchEnd, segments, depth + 1);
        }
    }

    segments.push_back(end);
}

void Lightning::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (!enabled) {
        return;
    }

    renderBolts(cmd, perFrameSet);
    renderFlash(cmd);
}

void Lightning::renderBolts(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (boltPipeline == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, boltPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, boltPipelineLayout,
        0, 1, &perFrameSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &boltDynamicVB, &offset);

    for (const auto& bolt : bolts) {
        if (!bolt.active || bolt.segments.empty()) {
            continue;
        }

        // Upload bolt segments to mapped buffer
        VkDeviceSize uploadSize = bolt.segments.size() * sizeof(glm::vec3);
        if (uploadSize > boltDynamicVBSize) {
            // Clamp to buffer size
            uploadSize = boltDynamicVBSize;
        }
        if (boltDynamicVBAllocInfo.pMappedData) {
            std::memcpy(boltDynamicVBAllocInfo.pMappedData, bolt.segments.data(), uploadSize);
        }

        // Push brightness
        vkCmdPushConstants(cmd, boltPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(float), &bolt.brightness);

        uint32_t vertexCount = static_cast<uint32_t>(uploadSize / sizeof(glm::vec3));
        vkCmdDraw(cmd, vertexCount, 1, 0, 0);
    }
}

void Lightning::renderFlash(VkCommandBuffer cmd) {
    if (!flash.active || flash.intensity <= 0.01f || flashPipeline == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flashPipeline);

    // Push flash intensity
    vkCmdPushConstants(cmd, flashPipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(float), &flash.intensity);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &flashQuadVB, &offset);
    vkCmdDraw(cmd, 4, 1, 0, 0);
}

void Lightning::setEnabled(bool enabled) {
    this->enabled = enabled;

    if (!enabled) {
        // Clear active effects
        for (auto& bolt : bolts) {
            bolt.active = false;
        }
        flash.active = false;
    }
}

void Lightning::setIntensity(float intensity) {
    this->intensity = glm::clamp(intensity, 0.0f, 1.0f);
}

} // namespace rendering
} // namespace wowee
