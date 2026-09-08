#include "rendering/glue_scene_effects.hpp"
#include "rendering/m2_renderer.hpp"
#include "rendering/camera.hpp"
#include "rendering/vk_buffer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_texture.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace wowee::rendering {
namespace {
constexpr size_t MaxEmitters = 64;
constexpr size_t MaxParticles = 4000;
constexpr size_t MaxVertices = MaxParticles * 6 + MaxEmitters * 127 * 6;
constexpr size_t MaxTextures = MaxEmitters * 2;
struct Vertex { glm::vec3 position, color; float alpha; glm::vec2 uv; };
static_assert(sizeof(Vertex) == 36);
bool finite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool finite(const glm::mat4& m) {
    for (unsigned c = 0; c < 4; ++c)
        for (unsigned r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r])) return false;
    return true;
}
struct Draw { VkDescriptorSet texture; uint8_t blend; uint32_t first, count; };

// This packet owns everything referenced by recorded FX commands. Clear/reload
// retains it until ALL frame fences have completed, including its textures.
struct Resources {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::array<VkPipeline, 7> pipelines{};
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> buffers;
    std::vector<std::unique_ptr<VkTexture>> textures;
    std::vector<VkDescriptorSet> textureSets;
    ~Resources() {
        if (!device) return;
        for (auto p : pipelines) if (p) vkDestroyPipeline(device, p, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
        if (textureLayout) vkDestroyDescriptorSetLayout(device, textureLayout, nullptr);
    }
};
VkPipelineColorBlendAttachmentState blendFor(uint8_t mode) {
    auto b = PipelineBuilder::blendAlpha();
    switch (mode) {
    case 0: case 1: b.blendEnable = VK_FALSE; break;
    case 3: // Add (source color, destination color)
        b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case 4: // AddAlpha
        b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case 5: // Modulate
        b.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
    case 6: // Modulate2x
        b.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
        break;
    default: break;
    }
    return b;
}
} // namespace

struct GlueSceneEffects::State {
    VkContext* context = nullptr;
    // Never initialize this renderer: only its existing CPU samplers/simulation
    // are used. No world geometry, world buffers or animation threads are made.
    M2Renderer simulator;
    M2ModelGPU model;
    M2Instance instance{};
    std::shared_ptr<Resources> gpu;
    std::vector<VkDescriptorSet> particleSets, ribbonSets;
    std::vector<uint8_t> ribbonBlends;
    std::vector<Vertex> vertices;
    std::vector<Draw> draws;
    bool firstDraw = true;
    float diagnosticSeconds = 0;
    bool motionDiagnosed = false;
    ~State() {
        // Also covers partial load failures after an upload entered a batch.
        if (gpu && context) {
            auto keep = std::move(gpu);
            context->deferAfterAllFrameFences([keep = std::move(keep)] {});
        }
    }
};

GlueSceneEffects::GlueSceneEffects() = default;
GlueSceneEffects::~GlueSceneEffects() { clear(); }
bool GlueSceneEffects::loaded() const { return state_ && state_->gpu; }
size_t GlueSceneEffects::particleCount() const {
    return state_ ? state_->instance.particles.size() : 0;
}
size_t GlueSceneEffects::ribbonEdgeCount() const {
    size_t n = 0;
    if (state_) for (const auto& edges : state_->instance.ribbonEdges) n += edges.size();
    return n;
}
void GlueSceneEffects::clear() {
    if (!state_) return;
    if (state_->gpu && state_->context) {
        auto keep = std::move(state_->gpu);
        state_->context->deferAfterAllFrameFences([keep = std::move(keep)] {});
    }
    state_.reset();
}

bool GlueSceneEffects::load(const pipeline::M2Model& scene, VkContext* context,
    VkDescriptorSetLayout perFrameLayout, pipeline::AssetManager* assets,
    VkRenderPass renderPass, VkSampleCountFlagBits samples) {
    clear();
    if (scene.particleEmitters.empty() && scene.ribbonEmitters.empty()) return true;
    if (!context || !assets || !perFrameLayout || !renderPass) return false;
    if (scene.particleEmitters.size() > MaxEmitters || scene.ribbonEmitters.size() > MaxEmitters) {
        LOG_ERROR("Glue FX: emitter limit exceeded for ", scene.name,
                  " particles=", scene.particleEmitters.size(), " ribbons=", scene.ribbonEmitters.size());
        return false;
    }
    auto s = std::make_unique<State>();
    s->context = context;
    s->model.name = scene.name;
    s->model.isGlueScene = true;
    s->model.particleEmitters = scene.particleEmitters;
    s->model.ribbonEmitters = scene.ribbonEmitters;
    s->model.globalSequenceDurations = scene.globalSequenceDurations;
    s->instance.cachedModel = &s->model;
    s->instance.modelMatrix = glm::mat4(1.0f);
    s->instance.scale = 1.0f;
    s->instance.particles.reserve(MaxParticles);
    s->vertices.reserve(MaxVertices);
    s->draws.reserve(MaxTextures);
    s->gpu = std::make_shared<Resources>();
    auto& gpu = *s->gpu;
    gpu.device = context->getDevice();
    VkDescriptorSetLayoutBinding textureBinding{};
    textureBinding.binding = 0;
    textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureBinding.descriptorCount = 1;
    textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.bindingCount = 1; dci.pBindings = &textureBinding;
    if (vkCreateDescriptorSetLayout(gpu.device, &dci, nullptr, &gpu.textureLayout) != VK_SUCCESS) return false;
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(MaxTextures)};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = MaxTextures; pci.poolSizeCount = 1; pci.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(gpu.device, &pci, nullptr, &gpu.pool) != VK_SUCCESS) return false;
    VkDescriptorSetLayout layouts[]{perFrameLayout, gpu.textureLayout};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = 2; lci.pSetLayouts = layouts;
    if (vkCreatePipelineLayout(gpu.device, &lci, nullptr, &gpu.layout) != VK_SUCCESS) return false;
    VkShaderModule vert, frag;
    if (!vert.loadFromFile(gpu.device, "assets/shaders/m2_ribbon.vert.spv") ||
        !frag.loadFromFile(gpu.device, "assets/shaders/m2_ribbon.frag.spv")) return false;
    VkVertexInputBindingDescription vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attrs{
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,0}, {1,0,VK_FORMAT_R32G32B32_SFLOAT,12},
        {2,0,VK_FORMAT_R32_SFLOAT,24}, {3,0,VK_FORMAT_R32G32_SFLOAT,28}};
    for (uint8_t blend = 0; blend < gpu.pipelines.size(); ++blend) {
        gpu.pipelines[blend] = PipelineBuilder()
            .setShaders(vert.stageInfo(VK_SHADER_STAGE_VERTEX_BIT), frag.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
            .setVertexInput({vb}, attrs).setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
            .setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
            .setColorBlendAttachment(blendFor(blend)).setMultisample(samples)
            .setLayout(gpu.layout).setRenderPass(renderPass)
            .setDynamicStates({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
            .build(gpu.device, context->getPipelineCache());
        if (!gpu.pipelines[blend]) return false;
    }
    for (auto& buffer : gpu.buffers)
        if (!buffer.createMapped(context->getAllocator(), MaxVertices * sizeof(Vertex),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;

    std::unordered_map<uint16_t, VkDescriptorSet> cache;
    uint64_t textureBytes = 0;
    auto textureSet = [&](uint16_t index) -> VkDescriptorSet {
        if (auto found = cache.find(index); found != cache.end()) return found->second;
        if (index >= scene.textures.size() || scene.textures[index].filename.empty()) return VK_NULL_HANDLE;
        const auto& path = scene.textures[index].filename;
        auto image = assets->loadTexture(path, true);
        if (!image.isValid() || image.width > 4096 || image.height > 4096) {
            LOG_WARNING("Glue FX: missing or invalid authored texture ", path);
            cache[index] = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }
        const uint64_t bytes = uint64_t(image.width) * image.height * 4;
        if (textureBytes + bytes > 64u * 1024u * 1024u) {
            LOG_WARNING("Glue FX: texture memory budget reached at ", path);
            cache[index] = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }
        auto texture = std::make_unique<VkTexture>();
        if (!texture->uploadBLP(*context, image)) {
            image = assets->loadTexture(path, false);
            if (!image.isValid() || !texture->uploadBLP(*context, image)) return VK_NULL_HANDLE;
        }
        if (!texture->createSampler(gpu.device, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) || !texture->isValid()) return VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = gpu.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &gpu.textureLayout;
        if (vkAllocateDescriptorSets(gpu.device, &ai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
        auto info = texture->descriptorInfo();
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set; write.dstBinding = 0; write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &info;
        vkUpdateDescriptorSets(gpu.device, 1, &write, 0, nullptr);
        gpu.textures.push_back(std::move(texture)); gpu.textureSets.push_back(set);
        textureBytes += bytes;
        cache[index] = set;
        return set;
    };
    for (auto& emitter : s->model.particleEmitters) {
        auto set = textureSet(emitter.texture);
        s->particleSets.push_back(set);
        if (!set) emitter.enabled = false;
    }
    for (auto& emitter : s->model.ribbonEmitters) {
        auto set = textureSet(emitter.textureIndex);
        if (!set && emitter.textureIndex < scene.textureLookup.size())
            set = textureSet(scene.textureLookup[emitter.textureIndex]);
        s->ribbonSets.push_back(set);
        uint8_t mode = 2;
        if (emitter.materialIndex < scene.materials.size())
            mode = static_cast<uint8_t>(std::min<uint16_t>(scene.materials[emitter.materialIndex].blendMode, 6));
        s->ribbonBlends.push_back(mode);
        if (!set || !std::isfinite(emitter.edgeLifetime) || emitter.edgeLifetime <= 0.0f)
            emitter.edgesPerSecond = 0.0f;
    }
    LOG_INFO("Glue FX: authored effects ready scene=", scene.name,
             " particles=", s->model.particleEmitters.size(), " ribbons=", s->model.ribbonEmitters.size(),
             " textures=", gpu.textures.size(), " bufferBytes=", MaxVertices * sizeof(Vertex),
             " frameSlots=", MAX_FRAMES_IN_FLIGHT);
    state_ = std::move(s);
    return true;
}

void GlueSceneEffects::update(float deltaTime, const CharacterRenderer::SceneFrame& frame) {
    if (!loaded() || !frame.bones || frame.bones->size() > kMaxBonesPerInstance ||
        !finite(frame.model) || !std::isfinite(frame.timeMs) || !std::isfinite(frame.globalTimeMs)) return;
    for (const auto& bone : *frame.bones) if (!finite(bone)) return;
    auto& s = *state_;
    if (frame.globalTimeMs < s.instance.globalSequenceTime) {
        s.instance.particles.clear(); s.instance.ribbonEdges.clear();
        s.instance.emitterAccumulators.clear(); s.instance.ribbonEdgeAccumulators.clear();
    }
    s.instance.modelMatrix = frame.model;
    s.instance.position = glm::vec3(frame.model[3]);
    s.instance.boneMatrices = *frame.bones;
    s.instance.currentSequenceIndex = frame.sequence;
    s.instance.animTime = frame.timeMs;
    s.instance.globalSequenceTime = frame.globalTimeMs;
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) return;
    const float dt = std::min(deltaTime, 0.1f);
    s.simulator.updateParticles(s.instance, dt);
    s.simulator.emitParticles(s.instance, s.model, dt);
    s.simulator.updateRibbons(s.instance, s.model, dt);
    s.diagnosticSeconds += dt;
    if (!s.motionDiagnosed && s.diagnosticSeconds >= 5.0f) {
        size_t moving = 0;
        glm::vec3 minimum(1e30f), maximum(-1e30f);
        for (const auto& particle : s.instance.particles) {
            if (glm::dot(particle.velocity, particle.velocity) > 1e-8f) ++moving;
            minimum = glm::min(minimum, particle.position);
            maximum = glm::max(maximum, particle.position);
        }
        const glm::vec3 span = s.instance.particles.empty() ? glm::vec3(0) : maximum-minimum;
        LOG_INFO("Glue FX: motion scene=", s.model.name, " globalMs=", frame.globalTimeMs,
                 " particles=", s.instance.particles.size(), " moving=", moving,
                 " span=(", span.x, ",", span.y, ",", span.z, ")");
        s.motionDiagnosed = true;
    }
}

void GlueSceneEffects::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet,
                             const Camera& camera, uint32_t frameIndex) {
    if (!loaded() || !cmd || !perFrameSet || frameIndex >= MAX_FRAMES_IN_FLIGHT) return;
    auto& s = *state_; auto& gpu = *s.gpu;
    s.vertices.clear(); s.draws.clear();
    const auto right = camera.getRight(), up = camera.getUp();
    if (!finite(right) || !finite(up)) return;
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                    glm::vec3 color, float alpha, glm::vec2 uv0, glm::vec2 uv1) {
        if (s.vertices.size() + 6 > MaxVertices || !finite(a) || !finite(b) ||
            !finite(c) || !finite(d) || !finite(color) || !std::isfinite(alpha)) return;
        const Vertex v0{a,color,alpha,{uv0.x,uv0.y}}, v1{b,color,alpha,{uv1.x,uv0.y}};
        const Vertex v2{c,color,alpha,{uv1.x,uv1.y}}, v3{d,color,alpha,{uv0.x,uv1.y}};
        s.vertices.insert(s.vertices.end(), {v0,v1,v2,v0,v2,v3});
    };
    const float modelScale = glm::length(glm::vec3(s.instance.modelMatrix[0]));
    for (size_t ei = 0; ei < s.model.particleEmitters.size(); ++ei) {
        if (!s.particleSets[ei]) continue;
        const auto& em = s.model.particleEmitters[ei];
        const uint32_t first = static_cast<uint32_t>(s.vertices.size());
        for (const auto& particle : s.instance.particles) {
            if (particle.emitterIndex != static_cast<int>(ei)) continue;
            const float ratio = particle.life / std::max(particle.maxLife, 0.001f);
            const auto color = s.simulator.interpFBlockVec3(em.particleColor, ratio);
            const auto alpha = std::clamp(s.simulator.interpFBlockFloat(em.particleAlpha, ratio), 0.0f, 1.0f);
            const auto size = s.simulator.interpFBlockFloat(em.particleScale, ratio) * modelScale;
            if (!std::isfinite(size) || size <= 0.0f || size > 10000.0f) continue;
            const auto r = right * size * 0.5f, u = up * size * 0.5f, p = particle.position;
            const uint32_t cols = std::max<uint16_t>(em.textureCols, 1), rows = std::max<uint16_t>(em.textureRows, 1);
            const uint32_t count = cols * rows;
            uint32_t tile = static_cast<uint32_t>(std::max(particle.tileIndex, 0.0f)) % count;
            if ((em.flags & 0x80u) && count > 1)
                tile = (tile + static_cast<uint32_t>(std::fmod(std::max(s.instance.animTime, 0.0f) * 0.001f * count,
                                                               static_cast<float>(count)))) % count;
            const glm::vec2 uv0(static_cast<float>(tile % cols) / cols, static_cast<float>(tile / cols) / rows);
            quad(p-r+u,p+r+u,p+r-u,p-r-u,color,alpha,uv0,uv0+glm::vec2(1.0f/cols,1.0f/rows));
        }
        const auto count = static_cast<uint32_t>(s.vertices.size()) - first;
        if (count) s.draws.push_back({s.particleSets[ei], static_cast<uint8_t>(std::min<uint8_t>(em.blendingType,6)), first,count});
    }
    for (size_t ri = 0; ri < s.instance.ribbonEdges.size(); ++ri) {
        if (ri >= s.ribbonSets.size() || !s.ribbonSets[ri]) continue;
        const auto& edges = s.instance.ribbonEdges[ri];
        const uint32_t first = static_cast<uint32_t>(s.vertices.size());
        for (size_t i = 1; i < edges.size(); ++i) {
            const auto& a = edges[i-1]; const auto& b = edges[i];
            const float life = std::max(s.model.ribbonEmitters[ri].edgeLifetime, 0.001f);
            const float alpha = std::clamp(b.alpha * (1.0f-b.age/life),0.0f,1.0f);
            const float u0 = static_cast<float>(i-1)/static_cast<float>(edges.size()-1);
            const float u1 = static_cast<float>(i)/static_cast<float>(edges.size()-1);
            quad(a.worldPos+up*a.heightAbove*modelScale,b.worldPos+up*b.heightAbove*modelScale,
                 b.worldPos-up*b.heightBelow*modelScale,a.worldPos-up*a.heightBelow*modelScale,
                 b.color,alpha,{u0,0.0f},{u1,1.0f});
        }
        const auto count = static_cast<uint32_t>(s.vertices.size()) - first;
        if (count) s.draws.push_back({s.ribbonSets[ri],s.ribbonBlends[ri],first,count});
    }
    if (s.vertices.empty()) return;
    auto& buffer = gpu.buffers[frameIndex];
    if (!buffer.isValid() || !buffer.getMappedData() || s.vertices.size()*sizeof(Vertex) > buffer.getSize()) return;
    // One upload, disjoint ranges, no overwrites after recording a draw.
    std::memcpy(buffer.getMappedData(), s.vertices.data(), s.vertices.size()*sizeof(Vertex));
    ::VkBuffer handle = buffer.getBuffer(); VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd,0,1,&handle,&offset);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,gpu.layout,0,1,&perFrameSet,0,nullptr);
    for (const auto& draw : s.draws) {
        if (draw.first + draw.count > s.vertices.size() || !draw.texture) continue;
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,gpu.pipelines[draw.blend]);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,gpu.layout,1,1,&draw.texture,0,nullptr);
        vkCmdDraw(cmd,draw.count,1,draw.first,0);
    }
    if (s.firstDraw) {
        LOG_INFO("Glue FX: first authored draw scene=",s.model.name," particles=",particleCount(),
                 " ribbonEdges=",ribbonEdgeCount()," vertices=",s.vertices.size()," draws=",s.draws.size());
        s.firstDraw = false;
    }
}
} // namespace wowee::rendering
