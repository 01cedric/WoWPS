#include "rendering/frustum.hpp"
#include "rendering/shadow_fit.hpp"
#include "rendering/shadow_receiver_hull.hpp"
#include "../../extern/vk_mem_alloc.h"
#include "rendering/sun_direction.hpp"
#include "rendering/vk_frame_data.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

using namespace wowee::rendering;
// Verify the actual host structure against the receiving GLSL std140 contract.
static_assert(offsetof(GPUPerFrameData, lightSpaceMatrix) == 128);
static_assert(offsetof(GPUPerFrameData, lightDir) == 192);
static_assert(offsetof(GPUPerFrameData, shadowParams) == 288);
static_assert(offsetof(GPUPerFrameData, localLightPosRadius) == 336);
static_assert(offsetof(GPUPerFrameData, localLightMeta) == 2384);
static_assert(offsetof(GPUPerFrameData, nearLightSpaceMatrix) == 2400);
static_assert(offsetof(GPUPerFrameData, shadowAtlasParams) == 2464);
static_assert(sizeof(GPUPerFrameData) == 2480);

struct TestCamera {
    glm::vec3 position{-8940, -130, 90};
    glm::vec3 forward = glm::normalize(glm::vec3(1, .2f, -.1f));
    glm::vec3 getPosition() const { return position; }
    glm::vec3 getForward() const { return forward; }
    float getFovDegrees() const { return 45; }
    float getAspectRatio() const { return 16.f / 9; }
    float getNearPlane() const { return 1; }
};
struct TestLighting {
    struct Params { glm::vec3 directionalDir{1, 0, -.1f}; } params;
    const Params& getLightingParams() const { return params; }
};
struct Renderer {
    TestCamera* camera = nullptr;
    TestLighting* lightingManager = nullptr;
    float shadowDistance_ = 240;
    bool shadowCenterInitialized = false;
    glm::vec3 characterPosition{-8940, -130, 90}, shadowCenter{};
    float shadowHalfExtent_ = 0;
    static constexpr float kNearShadowDistance = 48.0f;
    float nearShadowHalfExtent_ = 0;
    glm::vec3 nearShadowCenter_{};
    glm::mat4 nearLightSpaceMatrix_{1.0f};
    uint32_t SHADOW_MAP_SIZE = 1024;
    glm::mat4 computeLightSpaceMatrix();
};
template<class T> T handle(uintptr_t id) { return reinterpret_cast<T>(id); }
struct TestChunk {
    VkBuffer vertexBuffer = handle<VkBuffer>(1), indexBuffer = handle<VkBuffer>(2);
    bool valid = true;
    bool isValid() const { return valid; }
    glm::vec3 boundingSphereCenter{};
    float boundingSphereRadius = 8;
    int32_t megaBaseVertex = 0;
    uint32_t megaFirstIndex = 0, indexCount = 0;
};
struct TestVkCtx {
    uint32_t frame = 0;
    uint32_t getCurrentFrame() const { return frame; }
    VmaAllocator getAllocator() const { return nullptr; }
};
struct TerrainRenderer {
    VkPipeline shadowPipeline_ = handle<VkPipeline>(1);
    VkPipelineLayout shadowPipelineLayout_ = handle<VkPipelineLayout>(1);
    struct { VkDescriptorSet set = handle<VkDescriptorSet>(1); } shadowParams_;
    VkBuffer megaVB_ = handle<VkBuffer>(3), megaIB_ = handle<VkBuffer>(4);
    TestVkCtx context; TestVkCtx* vkCtx = &context;
    static constexpr uint32_t MAX_INDIRECT_DRAWS = 8192;
    static constexpr uint32_t SHADOW_INDIRECT_SLICES = 4;
    std::vector<VkDrawIndexedIndirectCommand> indirectStorage =
        std::vector<VkDrawIndexedIndirectCommand>(MAX_INDIRECT_DRAWS * SHADOW_INDIRECT_SLICES);
    VkBuffer indirectBuffer_ = reinterpret_cast<VkBuffer>(indirectStorage.data());
    VmaAllocation indirectAlloc_ = reinterpret_cast<VmaAllocation>(1);
    void* indirectMapped_ = indirectStorage.data();
    std::vector<TestChunk> chunks;
    void renderShadow(VkCommandBuffer, const glm::mat4&, const glm::vec3&, float, uint32_t,
                      const ShadowReceiverHull* = nullptr);
};
static std::vector<uint32_t> submitted;
extern "C" {
void vkCmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline) {}
void vkCmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,
                            uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*) {}
void vkCmdPushConstants(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*) {}
void vkCmdBindVertexBuffers(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*) {}
void vkCmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType) {}
void vkCmdDrawIndexed(VkCommandBuffer, uint32_t count, uint32_t, uint32_t, int32_t, uint32_t) {
    submitted.push_back(count);
}
void vkCmdDrawIndexedIndirect(VkCommandBuffer, VkBuffer buffer, VkDeviceSize offset,
                              uint32_t drawCount, uint32_t stride) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(buffer) + offset;
    for (uint32_t i = 0; i < drawCount; ++i) {
        const auto* draw = reinterpret_cast<const VkDrawIndexedIndirectCommand*>(bytes + i * stride);
        submitted.push_back(draw->indexCount);
    }
}
VkResult vmaFlushAllocation(VmaAllocator, VmaAllocation, VkDeviceSize, VkDeviceSize) { return VK_SUCCESS; }
}
#define LOG_INFO(...) do {} while (0)
#include "shadow_production.inc"
#undef LOG_INFO

// Execute the backend viewport conversion, capturing its GNM scale/offset.
struct GnmSetViewportInfo {
    float dmin, dmax, scale[3], offset[3];
};
struct TestPipeline { bool dynamic_viewport = true; };
struct VkPs4CommandBuffer {
    TestPipeline* current_pipeline = nullptr;
    bool pipeline_rebind_required = false;
    VkViewport viewport0{};
    bool viewport0_valid = false;
    int gnm_cmd = 0;
};
static GnmSetViewportInfo viewportInfo{};
void sceGnmDrawCmdSetViewport(int*, uint32_t, const GnmSetViewportInfo* info) {
    viewportInfo = *info;
}
#include "viewport_production.inc"

static void near(float actual, float expected, float tolerance = .00005f) {
    assert(std::abs(actual - expected) < tolerance);
}
int main() {
    TestCamera camera;
    TestLighting lighting;
    Renderer renderer;
    renderer.camera = &camera;
    renderer.lightingManager = &lighting;
    VkPs4CommandBuffer command{};
    const VkViewport viewport{0, 0, 1024, 1024, 0, 1};
    vk_ps4_CmdSetViewport(reinterpret_cast<VkCommandBuffer>(&command), 0, 1, &viewport);
    for (const glm::vec3 direction : {glm::vec3(1, 0, -.1f), glm::vec3(1, 0, .1f),
                                      glm::vec3(0, 0, -1), glm::vec3(.3f, -.7f, -.6f)}) {
        lighting.params.directionalDir = direction;
        const auto matrix = renderer.computeLightSpaceMatrix();
        const auto travel = outdoorKeyLightTravelDirection(direction);
        const glm::vec3 center = renderer.shadowCenter;
        const auto point = [&](glm::vec3 p) { return matrix * glm::vec4(p, 1); };
        near(point(center).w, 1);
        near(point(center - travel * 719.f).z, 0); // eye at -720; near = 1
        near(point(center + travel * 840.f).z, 1); // far = 1560
        const auto receiver = point(center);
        const auto upstream = point(center - travel * 420.f);
        near(upstream.x, receiver.x);
        near(upstream.y, receiver.y);
        assert(upstream.z < receiver.z); // LESS_OR_EQUAL comparison gives shadow.
        near(receiver.z - upstream.z, 420.f / 1559.f);
        const glm::vec3 up = std::abs(travel.z) > .99f ? glm::vec3(0,1,0) : glm::vec3(0,0,1);
        const auto right = glm::normalize(glm::cross(travel, up));
        const auto lightUp = glm::normalize(glm::cross(right, travel));
        // Positive viewport height + projection Y flip + receiver UV mapping agree.
        near(point(center + right * renderer.shadowHalfExtent_).x, 1);
        near(point(center + lightUp * renderer.shadowHalfExtent_).y, -1);
        for (const glm::vec3 offset : {right * 33.f, lightUp * 55.f,
                                       -lightUp * 60.f, travel * 50.f}) {
            const auto projected = point(center + offset);
            // GLSL receiver UV must address the exact row/column rasterized
            // by the real backend viewport, not a vertically mirrored row.
            for (int axis = 0; axis != 2; ++axis) {
                const float rasterPixel = projected[axis] * viewportInfo.scale[axis] +
                                          viewportInfo.offset[axis];
                const float sampledPixel = (projected[axis] * .5f + .5f) * 1024.f;
                near(rasterPixel, sampledPixel, .0001f);
            }
            near(projected.z * viewportInfo.scale[2] + viewportInfo.offset[2], projected.z);
        }

        TerrainRenderer terrain;
        const auto add = [&](uint32_t id, glm::vec3 location, bool valid = true) {
            TestChunk chunk;
            chunk.indexCount = id;
            chunk.boundingSphereCenter = location;
            chunk.valid = valid;
            terrain.chunks.push_back(chunk);
        };
        add(1, center);
        add(2, center - travel * 420.f); // Upstream hillside missed by former sphere.
        add(3, center + right * (renderer.shadowHalfExtent_ + 60));
        add(4, center - travel * 800.f); // Before near plane.
        add(5, center + travel * 900.f); // Beyond far plane.
        add(6, center + right * (renderer.shadowHalfExtent_ + 4)); // Intersects side.
        add(7, center, false);
        assert(420.f > renderer.shadowHalfExtent_ * 1.35f + 8.f);
        submitted.clear();
        terrain.renderShadow(nullptr, matrix, center, renderer.shadowHalfExtent_ * 1.35f, 0);
        assert((submitted == std::vector<uint32_t>{1, 2, 6}));
        terrain.megaVB_ = VK_NULL_HANDLE; // Same geometry on fallback buffer path.
        submitted.clear();
        terrain.renderShadow(nullptr, matrix, center, renderer.shadowHalfExtent_ * 1.35f, 0);
        assert((submitted == std::vector<uint32_t>{1, 2, 6}));
    }
    std::puts("PASS production shadow projection/terrain/PS4 viewport: low sun, moon, overhead, normal light; world offsets; 0..1 depth; raster/sample XY agree; caster/receiver ordering; upstream retained, side/near/far rejected; UBO std140 offsets. CPU submission evidence only.");
}
