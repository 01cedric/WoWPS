#include "rendering/stream_load_timing.hpp"
#include "rendering/terrain_vertex.hpp"
#include "rendering/shadow_params.hpp"
#include "rendering/terrain_renderer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_buffer.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_utils.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/frustum.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "core/logger.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <cstring>

namespace wowee {
namespace rendering {

// Matches set 1 binding 7 in terrain.frag.glsl
struct TerrainParamsUBO {
    int32_t layerCount;
    int32_t hasLayer1;
    int32_t hasLayer2;
    int32_t hasLayer3;
};

TerrainRenderer::TerrainRenderer() = default;

TerrainRenderer::~TerrainRenderer() {
    shutdown();
}

/// Builds the fill pipeline and its wireframe derivative from a loaded shader
/// pair.
///
/// initialize() and recreatePipelines() both need exactly these two in exactly
/// these states, and each described both for itself. Answers whether the fill
/// pipeline built; the wireframe is a debug view, so its absence is a warning
/// rather than a failure. Destroying the shader modules is left to the caller,
/// which loaded them.
bool TerrainRenderer::buildMainPassPipelines(VkDevice device,
                                             wowee::rendering::VkShaderModule& vertShader,
                                             wowee::rendering::VkShaderModule& fragShader) {
    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding = perVertexBinding(sizeof(pipeline::TerrainVertex));
    const std::vector<VkVertexInputAttributeDescription> vertexAttribs =
        toVkAttributes(kTerrainVertexAttributes);

    // --- Build fill pipeline (base for derivatives - shared state optimization) ---
    VkRenderPass mainPass = vkCtx->getImGuiRenderPass();

    pipeline = PipelineBuilder()
        .setShaders(vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({ vertexBinding }, vertexAttribs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(mainPass)
        .setDynamicStates(viewportAndScissorDynamic())
        .setFlags(VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT)
        .build(device, vkCtx->getPipelineCache());

    if (!pipeline) {
        LOG_ERROR("TerrainRenderer: failed to create fill pipeline");
        return false;
    }

    // --- Build wireframe pipeline (derivative of fill) ---
    // VK_POLYGON_MODE_LINE needs fillModeNonSolid, which plenty of mobile GPUs
    // do not have. Asking anyway is a validation error and a null pipeline;
    // drawing falls back to the filled one either way.
    if (!vkCtx->isWireframeSupported()) {
        LOG_WARNING("TerrainRenderer: wireframe views need fillModeNonSolid, "
                    "which this device does not support");
        return true;
    }

    wireframePipeline = PipelineBuilder()
        .setShaders(vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({ vertexBinding }, vertexAttribs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE)
        .setDepthTest(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(mainPass)
        .setDynamicStates(viewportAndScissorDynamic())
        .setFlags(VK_PIPELINE_CREATE_DERIVATIVE_BIT)
        .setBasePipeline(pipeline)
        .build(device, vkCtx->getPipelineCache());

    if (!wireframePipeline) {
        LOG_WARNING("TerrainRenderer: wireframe pipeline not available");
    }

    return true;
}

bool TerrainRenderer::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout,
                                  pipeline::AssetManager* assets) {
    vkCtx = ctx;
    assetManager = assets;

    if (!vkCtx || !assetManager) {
        LOG_ERROR("TerrainRenderer: null context or asset manager");
        return false;
    }

    LOG_INFO("Initializing terrain renderer (Vulkan)");
    VkDevice device = vkCtx->getDevice();

    // --- Create material descriptor set layout (set 1) ---
    // bindings 0-6: combined image samplers (base + 3 layer + 3 alpha)
    // binding 7: uniform buffer (TerrainParams)
    std::vector<VkDescriptorSetLayoutBinding> materialBindings(8);
    for (uint32_t i = 0; i < 7; i++) {
        materialBindings[i] = {};
        materialBindings[i].binding = i;
        materialBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialBindings[i].descriptorCount = 1;
        materialBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    materialBindings[7] = {};
    materialBindings[7].binding = 7;
    materialBindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    materialBindings[7].descriptorCount = 1;
    materialBindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    materialSetLayout = createDescriptorSetLayout(device, materialBindings);
    if (!materialSetLayout) {
        LOG_ERROR("TerrainRenderer: failed to create material set layout");
        return false;
    }

    // --- Create descriptor pool ---
    VkDescriptorPoolSize poolSizes[] = {
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = MAX_MATERIAL_SETS * 7 },
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = MAX_MATERIAL_SETS },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = MAX_MATERIAL_SETS;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &materialDescPool) != VK_SUCCESS) {
        LOG_ERROR("TerrainRenderer: failed to create descriptor pool");
        return false;
    }

    // --- Create pipeline layout ---
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(GPUPushConstants);

    std::vector<VkDescriptorSetLayout> setLayouts = { perFrameLayout, materialSetLayout };
    pipelineLayout = createPipelineLayout(device, setLayouts, { pushRange });
    if (!pipelineLayout) {
        LOG_ERROR("TerrainRenderer: failed to create pipeline layout");
        return false;
    }

    // --- Load shaders ---
    VkShaderModule vertShader, fragShader;
    if (!vertShader.loadFromFile(device, "assets/shaders/terrain.vert.spv")) {
        LOG_ERROR("TerrainRenderer: failed to load vertex shader");
        return false;
    }
    if (!fragShader.loadFromFile(device, "assets/shaders/terrain.frag.spv")) {
        LOG_ERROR("TerrainRenderer: failed to load fragment shader");
        return false;
    }

    // --- Vertex input ---
    if (!buildMainPassPipelines(device, vertShader, fragShader)) {
        vertShader.destroy();
        fragShader.destroy();
        return false;
    }

    vertShader.destroy();
    fragShader.destroy();

    // --- Create fallback textures ---
    whiteTexture = std::make_unique<VkTexture>();
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    whiteTexture->upload(*vkCtx, whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);
    whiteTexture->createSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                 VK_SAMPLER_ADDRESS_MODE_REPEAT);

    opaqueAlphaTexture = std::make_unique<VkTexture>();
    uint8_t opaqueAlpha = 255;
    opaqueAlphaTexture->upload(*vkCtx, &opaqueAlpha, 1, 1, VK_FORMAT_R8_UNORM, false);
    opaqueAlphaTexture->createSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    transparentAlphaTexture = std::make_unique<VkTexture>();
    uint8_t transparentAlpha = 0;
    transparentAlphaTexture->upload(*vkCtx, &transparentAlpha, 1, 1, VK_FORMAT_R8_UNORM, false);
    transparentAlphaTexture->createSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    textureCacheBudgetBytes_ =
        envSizeMBOrDefault("WOWEE_TERRAIN_TEX_CACHE_MB", 4096) * 1024ull * 1024ull;
    LOG_INFO("Terrain texture cache budget: ", textureCacheBudgetBytes_ / (1024 * 1024), " MB");

    // Allocate mega vertex/index buffers and indirect draw buffer.
    // All terrain chunks share these buffers, eliminating per-chunk VB/IB rebinds.
    {
        VmaAllocator allocator = vkCtx->getAllocator();

        // Mega vertex buffer (host-visible for direct write during chunk upload)
        VkBufferCreateInfo vbCI{};
        vbCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbCI.size = static_cast<VkDeviceSize>(MEGA_VB_MAX_VERTS) * sizeof(pipeline::TerrainVertex);
        vbCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo vbAllocCI{};
        vbAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        vbAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo vbInfo{};
        if (vmaCreateBuffer(allocator, &vbCI, &vbAllocCI,
                &megaVB_, &megaVBAlloc_, &vbInfo) == VK_SUCCESS) {
            megaVBMapped_ = vbInfo.pMappedData;
        } else {
            LOG_WARNING("TerrainRenderer: mega VB allocation failed, per-chunk fallback");
        }

        // Mega index buffer
        VkBufferCreateInfo ibCI{};
        ibCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibCI.size = static_cast<VkDeviceSize>(MEGA_IB_MAX_INDICES) * sizeof(uint32_t);
        ibCI.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        VmaAllocationCreateInfo ibAllocCI{};
        ibAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ibAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo ibInfo{};
        if (vmaCreateBuffer(allocator, &ibCI, &ibAllocCI,
                &megaIB_, &megaIBAlloc_, &ibInfo) == VK_SUCCESS) {
            megaIBMapped_ = ibInfo.pMappedData;
        } else {
            LOG_WARNING("TerrainRenderer: mega IB allocation failed, per-chunk fallback");
        }

        // Indirect draw command buffer
        VkBufferCreateInfo indCI{};
        indCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indCI.size = static_cast<VkDeviceSize>(MAX_INDIRECT_DRAWS) *
                     SHADOW_INDIRECT_SLICES * sizeof(VkDrawIndexedIndirectCommand);
        indCI.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        VmaAllocationCreateInfo indAllocCI{};
        indAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        indAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo indInfo{};
        if (vmaCreateBuffer(allocator, &indCI, &indAllocCI,
                &indirectBuffer_, &indirectAlloc_, &indInfo) == VK_SUCCESS) {
            indirectMapped_ = indInfo.pMappedData;
        } else {
            LOG_WARNING("TerrainRenderer: indirect buffer allocation failed");
        }

        LOG_INFO("Terrain mega buffers: VB=", vbCI.size / (1024*1024), "MB IB=",
                 ibCI.size / (1024*1024), "MB indirect=",
                 indCI.size / 1024, "KB");
    }

    LOG_INFO("Terrain renderer initialized (Vulkan)");
    return true;
}

void TerrainRenderer::recreatePipelines() {
    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();

    // Destroy old pipelines (keep layouts)
    destroy(device, pipeline);
    destroy(device, wireframePipeline);

    // Load shaders
    VkShaderModule vertShader, fragShader;
    if (!vertShader.loadFromFile(device, "assets/shaders/terrain.vert.spv")) {
        LOG_ERROR("TerrainRenderer::recreatePipelines: failed to load vertex shader");
        return;
    }
    if (!fragShader.loadFromFile(device, "assets/shaders/terrain.frag.spv")) {
        LOG_ERROR("TerrainRenderer::recreatePipelines: failed to load fragment shader");
        vertShader.destroy();
        return;
    }

    buildMainPassPipelines(device, vertShader, fragShader);

    vertShader.destroy();
    fragShader.destroy();
}

void TerrainRenderer::shutdown() {
    LOG_INFO("Shutting down terrain renderer");

    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();
    VmaAllocator allocator = vkCtx->getAllocator();

    vkDeviceWaitIdle(device);

    clear();
    // clear() defers chunk destruction, and no further frames will run to drain
    // the queue - so run it now, while the descriptor pools those lambdas free
    // sets from are still alive. Without this every resident chunk's vertex and
    // index buffers outlived the device: twenty thousand of each.
    vkCtx->flushDeferredCleanup();

    for (auto& [path, entry] : textureCache) {
        if (entry.texture) entry.texture->destroy(device, allocator);
    }
    alphaReuseCache_.clear();
    alphaLookupCount_ = alphaOpaqueHits_ = 0;
    textureCache.clear();
    textureCacheBytes_ = 0;
    textureCacheCounter_ = 0;
    failedTextureCache_.clear();
    loggedTextureLoadFails_.clear();
    textureBudgetRejectWarnings_ = 0;

    if (whiteTexture) { whiteTexture->destroy(device, allocator); whiteTexture.reset(); }
    if (opaqueAlphaTexture) { opaqueAlphaTexture->destroy(device, allocator); opaqueAlphaTexture.reset(); }
    if (transparentAlphaTexture) { transparentAlphaTexture->destroy(device, allocator); transparentAlphaTexture.reset(); }

    destroy(device, pipeline);
    destroy(device, wireframePipeline);
    destroy(device, pipelineLayout);
    destroy(device, materialDescPool);
    destroy(device, materialSetLayout);

    // Shadow pipeline cleanup
    destroy(device, shadowPipeline_);
    destroy(device, shadowPipelineLayout_);
    destroyShadowParamsSet(device, allocator, shadowParams_);

    // Destroy mega buffers and indirect draw buffer
    if (megaVB_) { vmaDestroyBuffer(allocator, megaVB_, megaVBAlloc_); megaVB_ = VK_NULL_HANDLE; megaVBAlloc_ = VK_NULL_HANDLE; megaVBMapped_ = nullptr; }
    if (megaIB_) { vmaDestroyBuffer(allocator, megaIB_, megaIBAlloc_); megaIB_ = VK_NULL_HANDLE; megaIBAlloc_ = VK_NULL_HANDLE; megaIBMapped_ = nullptr; }
    if (indirectBuffer_) { vmaDestroyBuffer(allocator, indirectBuffer_, indirectAlloc_); indirectBuffer_ = VK_NULL_HANDLE; indirectAlloc_ = VK_NULL_HANDLE; indirectMapped_ = nullptr; }
    megaVBUsed_ = 0;
    megaIBUsed_ = 0;

    vkCtx = nullptr;
}

bool TerrainRenderer::loadTerrain(const pipeline::TerrainMesh& mesh,
                                   const std::vector<std::string>& texturePaths,
                                   int tileX, int tileY) {
    if (mesh.validChunkCount == 0) {
        LOG_WARNING("loadTerrain[", tileX, ",", tileY, "]: mesh has 0 valid chunks (", texturePaths.size(), " textures)");
        return false;
    }
    LOG_DEBUG("Loading terrain mesh: ", mesh.validChunkCount, " chunks");

    vkCtx->beginUploadBatch();

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const auto& chunk = mesh.getChunk(x, y);
            if (!chunk.isValid()) continue;

            TerrainChunkGPU gpuChunk = uploadChunk(chunk);
            if (!gpuChunk.isValid()) {
                LOG_WARNING("Failed to upload chunk [", x, ",", y, "]");
                continue;
            }

            calculateBoundingSphere(gpuChunk, chunk);

            // Load textures for this chunk
            bindChunkTextures(gpuChunk, chunk, texturePaths, tileX, tileY, x, y);

            gpuChunk.tileX = tileX;
            gpuChunk.tileY = tileY;

            // Create per-chunk params UBO
            // A failed allocation here is pressure rather than corruption, but
            // this path has no way to come back for the chunk, so it skips it.
            if (!createChunkParamsUBO(gpuChunk)) {
                LOG_WARNING("Terrain chunk UBO allocation failed - skipping chunk");
                destroyChunkGPU(gpuChunk);
                continue;
            }

            gpuChunk.materialSet = allocateMaterialSet();
            if (!gpuChunk.materialSet) {
                destroyChunkGPU(gpuChunk);
                continue;
            }
            if (!writeMaterialDescriptors(gpuChunk.materialSet, gpuChunk)) {
                destroyChunkGPU(gpuChunk);
                continue;
            }

            chunks.push_back(std::move(gpuChunk));
        }
    }

    vkCtx->endUploadBatch();

    LOG_DEBUG("Loaded ", chunks.size(), " terrain chunks to GPU");
    return !chunks.empty();
}

bool TerrainRenderer::loadTerrainIncremental(const pipeline::TerrainMesh& mesh,
                                              const std::vector<std::string>& texturePaths,
                                              int tileX, int tileY,
                                              int& chunkIndex, int maxChunksPerCall) {
    // Batch all GPU uploads (VBs, IBs, textures) into a single command buffer
    // submission with one fence wait, instead of one per buffer/texture.
    vkCtx->beginUploadBatch();

    int uploaded = 0;
    while (chunkIndex < 256 && uploaded < maxChunksPerCall) {
        int cy = chunkIndex / 16;
        int cx = chunkIndex % 16;
        chunkIndex++;

        const auto& chunk = mesh.getChunk(cx, cy);
        if (!chunk.isValid()) continue;

        TerrainChunkGPU gpuChunk = uploadChunk(chunk);
        if (!gpuChunk.isValid()) continue;

        calculateBoundingSphere(gpuChunk, chunk);

        StreamLoadStageScope materialTiming(activeStreamLoadTiming, StreamLoadStage::Materials);
        bindChunkTextures(gpuChunk, chunk, texturePaths, tileX, tileY, cx, cy);

        gpuChunk.tileX = tileX;
        gpuChunk.tileY = tileY;

        if (!createChunkParamsUBO(gpuChunk)) {
            LOG_WARNING("Terrain[", tileX, ",", tileY, "] chunk UBO allocation failed"
                        " - retrying next frame");
            destroyChunkGPU(gpuChunk);
            chunkIndex--;
            break;
        }

        gpuChunk.materialSet = allocateMaterialSet();
        if (!gpuChunk.materialSet) {
            destroyChunkGPU(gpuChunk);
            // Give the chunk back rather than dropping it. Both failures above
            // are pressure, not corruption: the descriptor pool is shared by
            // every resident tile and its sets come back only through
            // deferAfterAllFrameFences, so a tile arriving while an unloaded
            // one is still in flight can find the pool momentarily full. A
            // dropped chunk was never retried - the tile counted as loaded
            // with a hole in it, and the hole stayed for the rest of the
            // session. Stepping the index back and leaving means the caller
            // sees "not finished" and comes back once the frees have landed.
            chunkIndex--;
            break;
        }
        if (!writeMaterialDescriptors(gpuChunk.materialSet, gpuChunk)) {
            // Not the retry above: that one is descriptor-pool pressure, which
            // passes. This is the fallback textures being unsampleable, which
            // does not, so the chunk is dropped rather than asked for again.
            destroyChunkGPU(gpuChunk);
            continue;
        }

        chunks.push_back(std::move(gpuChunk));
        uploaded++;
    }

    {
        StreamLoadStageScope submitTiming(activeStreamLoadTiming, StreamLoadStage::UploadSubmit);
        vkCtx->endUploadBatch();
    }

    return chunkIndex >= 256;
}

void TerrainRenderer::bindChunkTextures(TerrainChunkGPU& gpuChunk,
                                        const pipeline::ChunkMesh& chunk,
                                        const std::vector<std::string>& texturePaths,
                                        int tileX, int tileY, int chunkX, int chunkY) {
    if (chunk.layers.empty()) {
        gpuChunk.baseTexture = whiteTexture.get();
        return;
    }

    uint32_t baseTexId = chunk.layers[0].textureId;
    if (baseTexId < texturePaths.size()) {
        gpuChunk.baseTexture = loadTexture(texturePaths[baseTexId]);
    } else {
        LOG_WARNING("Terrain[", tileX, ",", tileY, "] chunk[", chunkX, ",", chunkY,
                    "] base textureId ", baseTexId, " >= texturePaths size ",
                    texturePaths.size(), " - white fallback");
        gpuChunk.baseTexture = whiteTexture.get();
    }

    // Layer 0 is the base, so the three blended layers are 1..3.
#ifdef WOWEE_PS4
    std::array<TerrainAlphaCache<VkTexture>::Mask, 3> alphaMasks{};
    std::array<bool, 3> alphaNeedsImage{};
    std::array<bool, 3> alphaTransparent{};
    size_t nonTrivialAlphaCount = 0;
#endif
    for (size_t i = 1; i < chunk.layers.size() && i < 4; i++) {
        const auto& layer = chunk.layers[i];
        int li = static_cast<int>(i) - 1;

        VkTexture* layerTex = whiteTexture.get();
        if (layer.textureId < texturePaths.size()) {
            layerTex = loadTexture(texturePaths[layer.textureId]);
        } else {
            LOG_WARNING("Terrain[", tileX, ",", tileY, "] chunk[", chunkX, ",", chunkY,
                        "] layer[", i, "] textureId ", layer.textureId,
                        " >= texturePaths size ", texturePaths.size(),
                        " - white fallback");
        }
        gpuChunk.layerTextures[li] = layerTex;

#ifdef WOWEE_PS4
        // Normalise once. Completely opaque/transparent masks use the shared
        // 1x1 fallbacks; only authored gradients need a GPU image.
        if (!layer.alphaData.empty()) {
            alphaMasks[li] = TerrainAlphaCache<VkTexture>::normalize(layer.alphaData);
            const bool opaque = TerrainAlphaCache<VkTexture>::opaque(alphaMasks[li]);
            const bool transparent = !opaque && std::all_of(
                alphaMasks[li].begin(), alphaMasks[li].end(), [](uint8_t v) { return v == 0; });
            alphaTransparent[li] = transparent;
            alphaNeedsImage[li] = !opaque && !transparent;
            if (alphaNeedsImage[li]) ++nonTrivialAlphaCount;
        }
#else
        // A layer with no alpha map covers everything under it.
        VkTexture* alphaTex = opaqueAlphaTexture.get();
        if (!layer.alphaData.empty()) {
            alphaTex = createAlphaTexture(layer.alphaData);
        }
        gpuChunk.alphaTextures[li] = alphaTex;
#endif
        gpuChunk.layerCount = static_cast<int>(i);
    }

#ifdef WOWEE_PS4
    // The expensive hardware path is two/three small images per chunk, not
    // their 4 KiB payload. Pack them into one RGBA allocation and expose each
    // channel through an image-view swizzle. One non-trivial mask keeps the
    // existing R8 cache path because it is already one allocation and uses a
    // quarter of the pixel storage.
    bool packed = false;
    if (nonTrivialAlphaCount >= 2) {
        packed = createPackedAlphaTexture(gpuChunk, alphaMasks, alphaNeedsImage);
    }
    for (int li = 0; li < 3; ++li) {
        if (alphaTransparent[li]) {
            gpuChunk.alphaTextures[li] = transparentAlphaTexture.get();
            continue;
        }
        if (!alphaNeedsImage[li]) {
            gpuChunk.alphaTextures[li] = opaqueAlphaTexture.get();
            continue;
        }
        if (packed && gpuChunk.alphaPackViews[li] != VK_NULL_HANDLE) {
            // writeMaterialDescriptors uses the packed view directly.
            gpuChunk.alphaTextures[li] = nullptr;
            continue;
        }
        gpuChunk.alphaTextures[li] = createAlphaTexture(alphaMasks[li]);
    }
#endif
}

#ifdef WOWEE_PS4
bool TerrainRenderer::createPackedAlphaTexture(
    TerrainChunkGPU& gpuChunk,
    const std::array<TerrainAlphaCache<VkTexture>::Mask, 3>& masks,
    const std::array<bool, 3>& packed) {
    if (!vkCtx) return false;

    // Interleave three authored R8 masks into RGB. A stays opaque and is not
    // sampled. 64x64 RGBA is 16 KiB: slightly larger than three raw R8 masks,
    // but one image allocation instead of three is what removes the PS4 CPU
    // spike measured in 2.06.
    std::array<uint8_t, 64 * 64 * 4> pixels{};
    for (size_t p = 0; p < 64u * 64u; ++p) {
        pixels[p * 4 + 0] = packed[0] ? masks[0][p] : 255;
        pixels[p * 4 + 1] = packed[1] ? masks[1][p] : 255;
        pixels[p * 4 + 2] = packed[2] ? masks[2][p] : 255;
        pixels[p * 4 + 3] = 255;
    }

    auto texture = std::make_unique<VkTexture>();
    if (!texture->upload(*vkCtx, pixels.data(), 64, 64,
                         VK_FORMAT_R8G8B8A8_UNORM, false)) {
        return false;
    }
    if (!texture->createSampler(vkCtx->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)) {
        return false;
    }

    const VkComponentSwizzle channels[3] = {
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
    };
    VkImageView views[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    for (int li = 0; li < 3; ++li) {
        if (!packed[li]) continue;
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = texture->getImage();
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.components.r = channels[li];
        info.components.g = VK_COMPONENT_SWIZZLE_ZERO;
        info.components.b = VK_COMPONENT_SWIZZLE_ZERO;
        info.components.a = VK_COMPONENT_SWIZZLE_ONE;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        if (vkCreateImageView(vkCtx->getDevice(), &info, nullptr, &views[li]) != VK_SUCCESS) {
            for (VkImageView view : views)
                if (view) vkDestroyImageView(vkCtx->getDevice(), view, nullptr);
            return false;
        }
    }

    gpuChunk.alphaPackSampler = texture->getSampler();
    for (int li = 0; li < 3; ++li) gpuChunk.alphaPackViews[li] = views[li];
    gpuChunk.alphaPackTexture = std::move(texture);
    return true;
}
#endif

bool TerrainRenderer::createChunkParamsUBO(TerrainChunkGPU& gpuChunk) {
    TerrainParamsUBO params{};
    params.layerCount = gpuChunk.layerCount;
    params.hasLayer1 = gpuChunk.layerCount >= 1 ? 1 : 0;
    params.hasLayer2 = gpuChunk.layerCount >= 2 ? 1 : 0;
    params.hasLayer3 = gpuChunk.layerCount >= 3 ? 1 : 0;

    VkBufferCreateInfo bufCI{};
    bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCI.size = sizeof(TerrainParamsUBO);
    bufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapInfo{};
    // Check the return value - a null UBO handle would leave the GPU reading
    // from an invalid descriptor, crashing the driver under memory pressure
    // rather than losing one chunk.
    if (vmaCreateBuffer(vkCtx->getAllocator(), &bufCI, &allocCI,
                        &gpuChunk.paramsUBO, &gpuChunk.paramsAlloc, &mapInfo) != VK_SUCCESS) {
        return false;
    }
    if (mapInfo.pMappedData) {
        std::memcpy(mapInfo.pMappedData, &params, sizeof(params));
    }
    return true;
}

TerrainChunkGPU TerrainRenderer::uploadChunk(const pipeline::ChunkMesh& chunk) {
    StreamLoadStageScope geometryTiming(activeStreamLoadTiming, StreamLoadStage::Geometry);
    TerrainChunkGPU gpuChunk;

    gpuChunk.worldX = chunk.worldX;
    gpuChunk.worldY = chunk.worldY;
    gpuChunk.worldZ = chunk.worldZ;
    gpuChunk.indexCount = static_cast<uint32_t>(chunk.indices.size());
    gpuChunk.vertexCount = static_cast<uint32_t>(chunk.vertices.size());

    VkDeviceSize vbSize = chunk.vertices.size() * sizeof(pipeline::TerrainVertex);
    AllocatedBuffer vb = uploadBuffer(*vkCtx, chunk.vertices.data(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    gpuChunk.vertexBuffer = vb.buffer;
    gpuChunk.vertexAlloc = vb.allocation;

    VkDeviceSize ibSize = chunk.indices.size() * sizeof(pipeline::TerrainIndex);
    AllocatedBuffer ib = uploadBuffer(*vkCtx, chunk.indices.data(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    gpuChunk.indexBuffer = ib.buffer;
    gpuChunk.indexAlloc = ib.allocation;

    // Also copy into mega buffers for indirect drawing
    uint32_t vertCount = static_cast<uint32_t>(chunk.vertices.size());
    uint32_t idxCount = static_cast<uint32_t>(chunk.indices.size());
    if (megaVBMapped_ && megaIBMapped_ &&
        megaVBUsed_ + vertCount <= MEGA_VB_MAX_VERTS &&
        megaIBUsed_ + idxCount <= MEGA_IB_MAX_INDICES) {
        // Copy vertices
        auto* vbDst = static_cast<pipeline::TerrainVertex*>(megaVBMapped_) + megaVBUsed_;
        std::memcpy(vbDst, chunk.vertices.data(), vertCount * sizeof(pipeline::TerrainVertex));
        // Copy indices
        auto* ibDst = static_cast<uint32_t*>(megaIBMapped_) + megaIBUsed_;
        std::memcpy(ibDst, chunk.indices.data(), idxCount * sizeof(uint32_t));

        gpuChunk.megaBaseVertex = static_cast<int32_t>(megaVBUsed_);
        gpuChunk.megaFirstIndex = megaIBUsed_;
        megaVBUsed_ += vertCount;
        megaIBUsed_ += idxCount;
    }

    return gpuChunk;
}

VkTexture* TerrainRenderer::loadTexture(const std::string& path) {
    StreamLoadStageScope lookupTiming(activeStreamLoadTiming, StreamLoadStage::TextureLookup);
    auto normalizeKey = [](std::string key) {
        std::replace(key.begin(), key.end(), '/', '\\');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    };
    std::string key = normalizeKey(path);

    auto it = textureCache.find(key);
    if (it != textureCache.end()) {
        it->second.lastUse = ++textureCacheCounter_;
        if (activeStreamLoadTiming) ++activeStreamLoadTiming->textureHits;
        return it->second.texture.get();
    }
    // Terrain tileset textures are sampled and never read back, so they go
    // up as blocks with the file's own mip levels.
    pipeline::BLPImage blp;
    {
        StreamLoadStageScope readTiming(activeStreamLoadTiming, StreamLoadStage::TextureRead);
        if (activeStreamLoadTiming) ++activeStreamLoadTiming->syncTextures;
        blp = assetManager->loadTexture(key, true);
    }
    if (!blp.isValid()) {
        // Return white fallback but don't cache the failure - allow retry
        // on next tile load in case the asset becomes available.
        if (loggedTextureLoadFails_.insert(key).second) {
            LOG_WARNING("Failed to load texture: ", path);
        }
        return whiteTexture.get();
    }

    const size_t approxBytes = pipeline::textureUploadBudgetBytes(blp);
    if (textureCacheBytes_ + approxBytes > textureCacheBudgetBytes_) {
        if (textureBudgetRejectWarnings_ < 3) {
            LOG_WARNING("Terrain texture cache full (", textureCacheBytes_ / (1024 * 1024),
                        " MB / ", textureCacheBudgetBytes_ / (1024 * 1024),
                        " MB), rejecting texture: ", path);
        }
        ++textureBudgetRejectWarnings_;
        return whiteTexture.get();
    }

    auto tex = std::make_unique<VkTexture>();
    bool uploaded;
    {
        StreamLoadStageScope uploadTiming(activeStreamLoadTiming, StreamLoadStage::DiffuseUpload);
        uploaded = tex->uploadBLP(*vkCtx, blp);
    }
    if (!uploaded) {
        LOG_WARNING("Failed to upload texture to GPU: ", path);
        return whiteTexture.get();
    }
    tex->createSampler(vkCtx->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                        VK_SAMPLER_ADDRESS_MODE_REPEAT);

    VkTexture* raw = tex.get();
    TextureCacheEntry e;
    e.texture = std::move(tex);
    e.approxBytes = approxBytes;
    e.lastUse = ++textureCacheCounter_;
    textureCacheBytes_ += e.approxBytes;
    textureCache[key] = std::move(e);

    return raw;
}

void TerrainRenderer::uploadPreloadedTextures(
    const std::unordered_map<std::string, pipeline::BLPImage>& textures) {
    auto normalizeKey = [](std::string key) {
        std::replace(key.begin(), key.end(), '/', '\\');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    };
    // Batch all texture uploads into a single command buffer submission
    vkCtx->beginUploadBatch();

    for (const auto& [path, blp] : textures) {
        std::string key = normalizeKey(path);
        if (textureCache.find(key) != textureCache.end()) {
            if (activeStreamLoadTiming) ++activeStreamLoadTiming->textureHits;
            continue;
        }
        if (!blp.isValid()) continue;

        auto tex = std::make_unique<VkTexture>();
        bool uploaded;
        {
            StreamLoadStageScope uploadTiming(activeStreamLoadTiming, StreamLoadStage::DiffuseUpload);
            if (activeStreamLoadTiming) ++activeStreamLoadTiming->preparedTextures;
            uploaded = tex->uploadBLP(*vkCtx, blp);
        }
        if (!uploaded) continue;
        tex->createSampler(vkCtx->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                            VK_SAMPLER_ADDRESS_MODE_REPEAT);

        TextureCacheEntry e;
        e.texture = std::move(tex);
        e.approxBytes = pipeline::textureUploadBudgetBytes(blp);
        e.lastUse = ++textureCacheCounter_;
        textureCacheBytes_ += e.approxBytes;
        textureCache[key] = std::move(e);
    }

    {
        StreamLoadStageScope submitTiming(activeStreamLoadTiming, StreamLoadStage::UploadSubmit);
        vkCtx->endUploadBatch();
    }
}

VkTexture* TerrainRenderer::createAlphaTexture(const std::vector<uint8_t>& alphaData) {
    if (alphaData.empty()) return opaqueAlphaTexture.get();

    const auto mask = TerrainAlphaCache<VkTexture>::normalize(alphaData);
#ifdef WOWEE_PS4
    return createAlphaTexture(mask);
#else
    ++alphaLookupCount_;
    const bool opaque = TerrainAlphaCache<VkTexture>::opaque(mask);
    VkTexture* reused = nullptr;
    if (opaque) { ++alphaOpaqueHits_; reused = opaqueAlphaTexture.get(); }
    else reused = alphaReuseCache_.find(mask);
    if (alphaLookupCount_ <= 4 || alphaLookupCount_ % 256 == 0) {
        LOG_INFO("[TERRAIN_ALPHA_REUSE] requests=", alphaLookupCount_,
                 " exactHits=", alphaReuseCache_.hits, " opaqueHits=", alphaOpaqueHits_,
                 " misses=", alphaReuseCache_.misses, " replacements=", alphaReuseCache_.replacements,
                 " lookupBytes=", alphaReuseCache_.allocatedBytes(),
                 " avoidedImageUploads=", alphaReuseCache_.hits + alphaOpaqueHits_);
    }
    if (reused) return reused;
    const uint8_t* src = mask.data();

    auto tex = std::make_unique<VkTexture>();
    if (!tex->upload(*vkCtx, src, 64, 64, VK_FORMAT_R8_UNORM, false))
        return opaqueAlphaTexture.get();
    tex->createSampler(vkCtx->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    VkTexture* raw = tex.get();
    static uint64_t alphaCounter = 0;
    std::string key = "__alpha_" + std::to_string(++alphaCounter);
    TextureCacheEntry e;
    e.texture = std::move(tex);
    e.approxBytes = 64 * 64;
    e.lastUse = ++textureCacheCounter_;
    textureCacheBytes_ += e.approxBytes;
    textureCache[key] = std::move(e);
    alphaReuseCache_.remember(mask, raw);
    return raw;
#endif
}

#ifdef WOWEE_PS4
VkTexture* TerrainRenderer::createAlphaTexture(const TerrainAlphaCache<VkTexture>::Mask& mask) {
    ++alphaLookupCount_;
    const bool opaque = TerrainAlphaCache<VkTexture>::opaque(mask);
    VkTexture* reused = nullptr;
    if (opaque) { ++alphaOpaqueHits_; reused = opaqueAlphaTexture.get(); }
    else reused = alphaReuseCache_.find(mask);
    if (alphaLookupCount_ <= 4 || alphaLookupCount_ % 256 == 0) {
        LOG_INFO("[TERRAIN_ALPHA_REUSE] requests=", alphaLookupCount_,
                 " exactHits=", alphaReuseCache_.hits, " opaqueHits=", alphaOpaqueHits_,
                 " misses=", alphaReuseCache_.misses, " replacements=", alphaReuseCache_.replacements,
                 " lookupBytes=", alphaReuseCache_.allocatedBytes(),
                 " avoidedImageUploads=", alphaReuseCache_.hits + alphaOpaqueHits_);
    }
    if (reused) return reused;

    auto tex = std::make_unique<VkTexture>();
    if (!tex->upload(*vkCtx, mask.data(), 64, 64, VK_FORMAT_R8_UNORM, false))
        return opaqueAlphaTexture.get();
    tex->createSampler(vkCtx->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    VkTexture* raw = tex.get();
    static uint64_t alphaCounter = 0;
    std::string key = "__alpha_ps4_" + std::to_string(++alphaCounter);
    TextureCacheEntry e;
    e.texture = std::move(tex);
    e.approxBytes = 64 * 64;
    e.lastUse = ++textureCacheCounter_;
    textureCacheBytes_ += e.approxBytes;
    textureCache[key] = std::move(e);
    alphaReuseCache_.remember(mask, raw);
    return raw;
}
#endif

VkDescriptorSet TerrainRenderer::allocateMaterialSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = materialDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &materialSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(vkCtx->getDevice(), &allocInfo, &set) != VK_SUCCESS) {
        static uint64_t failCount = 0;
        ++failCount;
        if (failCount <= 8 || (failCount % 256) == 0) {
            LOG_WARNING("TerrainRenderer: failed to allocate material descriptor set (count=", failCount, ")");
        }
        return VK_NULL_HANDLE;
    }
    return set;
}

bool TerrainRenderer::writeMaterialDescriptors(VkDescriptorSet set, const TerrainChunkGPU& chunk) {
    VkTexture* white = whiteTexture.get();
    VkTexture* opaque = opaqueAlphaTexture.get();

    // Valid, not merely non-null. descriptorInfo() returns the texture's
    // handles as they are, so one whose upload or view creation failed writes
    // VK_NULL_HANDLE into a live descriptor and declares
    // SHADER_READ_ONLY_OPTIMAL over it. Sampling that is undefined behaviour
    // and reaches an NVIDIA driver as a graphics engine exception and a lost
    // device rather than as anything this client can catch. See #123.
    //
    // A chunk texture failing is ordinary - it is what the cache does under
    // memory pressure - and the 1x1 fallbacks are what that case is for.
    const auto sampleable = [](VkTexture* t) { return t && t->isValid(); };
    const auto pick = [&](VkTexture* wanted, VkTexture* fallback) {
        return sampleable(wanted) ? wanted : fallback;
    };
    if (!sampleable(white) || !sampleable(opaque)) {
        // The fallbacks themselves are gone, so there is nothing safe to write.
        // The caller drops the chunk: a hole in the ground costs a view of one
        // tile, and a null image view costs the device.
        static bool told = false;
        if (!told) {
            told = true;
            LOG_ERROR("TerrainRenderer: the 1x1 fallback textures are not "
                      "sampleable, so no chunk can be drawn");
        }
        return false;
    }

    VkDescriptorImageInfo imageInfos[7];
    imageInfos[0] = pick(chunk.baseTexture, white)->descriptorInfo();
    for (int i = 0; i < 3; i++) {
        imageInfos[1 + i] = pick(chunk.layerTextures[i], white)->descriptorInfo();
#ifdef WOWEE_PS4
        if (chunk.alphaPackViews[i] != VK_NULL_HANDLE && chunk.alphaPackSampler != VK_NULL_HANDLE) {
            imageInfos[4 + i] = {};
            imageInfos[4 + i].sampler = chunk.alphaPackSampler;
            imageInfos[4 + i].imageView = chunk.alphaPackViews[i];
            imageInfos[4 + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        } else
#endif
        {
            imageInfos[4 + i] = pick(chunk.alphaTextures[i], opaque)->descriptorInfo();
        }
    }

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = chunk.paramsUBO;
    bufInfo.offset = 0;
    bufInfo.range = sizeof(TerrainParamsUBO);

    VkWriteDescriptorSet writes[8] = {};
    for (int i = 0; i < 7; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }
    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = set;
    writes[7].dstBinding = 7;
    writes[7].descriptorCount = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[7].pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(vkCtx->getDevice(), 8, writes, 0, nullptr);
    return true;
}

void TerrainRenderer::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, const Camera& camera) {
    if (chunks.empty() || !pipeline) {
        static int emptyLog = 0;
        if (++emptyLog <= 3)
            LOG_WARNING("TerrainRenderer::render: chunks=", chunks.size(), " pipeline=", (pipeline != VK_NULL_HANDLE));
        return;
    }

    // One-time diagnostic: log chunk nearest to camera
    static bool loggedDiag = false;
    if (!loggedDiag && !chunks.empty()) {
        loggedDiag = true;
        glm::vec3 cam = camera.getPosition();
        // Find chunk nearest to camera
        const TerrainChunkGPU* nearest = nullptr;
        float nearestDist = std::numeric_limits<float>::max();
        for (const auto& ch : chunks) {
            float dx = ch.boundingSphereCenter.x - cam.x;
            float dy = ch.boundingSphereCenter.y - cam.y;
            float dz = ch.boundingSphereCenter.z - cam.z;
            float d = dx*dx + dy*dy + dz*dz;
            if (d < nearestDist) { nearestDist = d; nearest = &ch; }
        }
        if (nearest) {
            float d2d = std::sqrt((nearest->boundingSphereCenter.x-cam.x)*(nearest->boundingSphereCenter.x-cam.x) +
                                  (nearest->boundingSphereCenter.y-cam.y)*(nearest->boundingSphereCenter.y-cam.y));
            LOG_INFO("Terrain diag: chunks=", chunks.size(),
                     " cam=(", cam.x, ",", cam.y, ",", cam.z, ")",
                     " nearest_center=(", nearest->boundingSphereCenter.x, ",", nearest->boundingSphereCenter.y, ",", nearest->boundingSphereCenter.z, ")",
                     " dist2d=", d2d, " dist3d=", std::sqrt(nearestDist),
                     " radius=", nearest->boundingSphereRadius,
                     " matSet=", (nearest->materialSet != VK_NULL_HANDLE ? "ok" : "NULL"));
        }
    }

    VkPipeline activePipeline = (wireframe && wireframePipeline) ? wireframePipeline : pipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                             0, 1, &perFrameSet, 0, nullptr);

    GPUPushConstants push{};
    push.model = glm::mat4(1.0f);
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(GPUPushConstants), &push);

    Frustum frustum;
    if (frustumCullingEnabled) {
        glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
        frustum.extractFromMatrix(viewProj);
    }

    glm::vec3 camPos = camera.getPosition();

    renderedChunks = 0;
    culledChunks = 0;
    furthestDrawnSq_ = 0.0f;

    // Use mega VB + IB when available.
    // Bind mega buffers once, then use direct draws with base vertex/index offsets.
    const bool useMegaBuffers = (megaVB_ && megaIB_);
    bool megaBuffersBound = false;
    if (useMegaBuffers) {
        VkDeviceSize megaOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &megaVB_, &megaOffset);
        vkCmdBindIndexBuffer(cmd, megaIB_, 0, VK_INDEX_TYPE_UINT32);
        megaBuffersBound = true;
    }

    for (const auto& chunk : chunks) {
        if (!chunk.isValid() || !chunk.materialSet) continue;

        float dx = chunk.boundingSphereCenter.x - camPos.x;
        float dy = chunk.boundingSphereCenter.y - camPos.y;
        float distSq = dx * dx + dy * dy;
        // A chunk that straddles the horizon must keep its near-side triangles.
        const float chunkDistance = maxViewDistance_ + std::max(0.0f, chunk.boundingSphereRadius);
        if (distSq > chunkDistance * chunkDistance) {
            culledChunks++;
            continue;
        }

        if (frustumCullingEnabled && !isChunkVisible(chunk, frustum)) {
            culledChunks++;
            continue;
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                 1, 1, &chunk.materialSet, 0, nullptr);

        if (useMegaBuffers && chunk.megaBaseVertex >= 0) {
            // Rebound if a fallback chunk bound its own buffers since. The mega
            // buffers are bound once before the loop, and a chunk without a
            // place in them binds its own - which stays bound for whatever comes
            // next. A chunk after that one then drew its mega offsets against a
            // single chunk's buffer: firstIndex 6,290,784 into 3,072 bytes,
            // which is what took the GPU down.
            if (!megaBuffersBound) {
                VkDeviceSize megaOffset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &megaVB_, &megaOffset);
                vkCmdBindIndexBuffer(cmd, megaIB_, 0, VK_INDEX_TYPE_UINT32);
                megaBuffersBound = true;
            }
            vkCmdDrawIndexed(cmd, chunk.indexCount, 1,
                             chunk.megaFirstIndex, chunk.megaBaseVertex, 0);
        } else {
            // Fallback: per-chunk VB/IB bind + direct draw
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &chunk.vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, chunk.indexCount, 1, 0, 0, 0);
            megaBuffersBound = false;
        }
        renderedChunks++;
        if (distSq > furthestDrawnSq_) furthestDrawnSq_ = distSq;
    }

}

bool TerrainRenderer::initializeShadow(VkRenderPass shadowRenderPass) {
    if (!vkCtx || shadowRenderPass == VK_NULL_HANDLE) return false;
    if (shadowPipeline_ != VK_NULL_HANDLE) return true;  // already initialised
    VkDevice device = vkCtx->getDevice();
    VmaAllocator allocator = vkCtx->getAllocator();

    if (!createShadowParamsSet(device, allocator, sizeof(ShadowParamsUBO),
                               whiteTexture->getImageView(),
                               whiteTexture->getSampler(), "TerrainRenderer",
                               shadowParams_)) {
        return false;
    }

    // Pipeline layout: set 0 = shadowParams_.layout, push 128 bytes (lightSpaceMatrix + model)
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset = 0;
    pc.size = 128;
    shadowPipelineLayout_ = createPipelineLayout(device, {shadowParams_.layout}, {pc});
    if (!shadowPipelineLayout_) {
        LOG_ERROR("TerrainRenderer: failed to create shadow pipeline layout");
        return false;
    }

    VkShaderModule vertShader, fragShader;
    if (!vertShader.loadFromFile(device, "assets/shaders/shadow.vert.spv")) {
        LOG_ERROR("TerrainRenderer: failed to load shadow vertex shader");
        return false;
    }
    if (!fragShader.loadFromFile(device, "assets/shaders/shadow.frag.spv")) {
        LOG_ERROR("TerrainRenderer: failed to load shadow fragment shader");
        vertShader.destroy();
        return false;
    }

    // The shadow shader is shared with the skinned renderers, so it declares
    // bone inputs terrain has none of; kTerrainShadowVertexAttributes says
    // where they point and why.
    const VkVertexInputBindingDescription vertBind =
        perVertexBinding(sizeof(pipeline::TerrainVertex));
    const std::vector<VkVertexInputAttributeDescription> vertAttrs =
        toVkAttributes(kTerrainShadowVertexAttributes);

    shadowPipeline_ = buildShadowPipeline(
        device, vkCtx->getPipelineCache(),
        vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
        fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
        vertBind, vertAttrs, shadowPipelineLayout_, shadowRenderPass);

    vertShader.destroy();
    fragShader.destroy();

    if (!shadowPipeline_) {
        LOG_ERROR("TerrainRenderer: failed to create shadow pipeline");
        return false;
    }
    LOG_INFO("TerrainRenderer shadow pipeline initialized");
    return true;
}

void TerrainRenderer::renderShadow(VkCommandBuffer cmd, const glm::mat4& lightSpaceMatrix,
                                    const glm::vec3& /*shadowCenter*/, float /*shadowRadius*/,
                                    uint32_t shadowPassIndex,
                                    const ShadowReceiverHull* receiverHull) {
    if (!shadowPipeline_ || !shadowParams_.set) return;
    if (chunks.empty()) return;

    // The receiver footprint is not the caster volume: an upstream hillside
    // can project into it at low sun even outside the old center/radius test.
    // Use the same finite light volume that clips the actual shadow draw.
    Frustum lightFrustum;
    lightFrustum.extractFromMatrix(lightSpaceMatrix);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_,
        0, 1, &shadowParams_.set, 0, nullptr);

    // Identity model matrix - terrain vertices are already in world space
    static const glm::mat4 identity(1.0f);
    ShadowPush push{ .lightSpaceMatrix = lightSpaceMatrix, .model = identity };
    vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, 128, &push);

    // The terrain mega buffers are ideal for indexed indirect shadow submission:
    // every visible chunk shares the same pipeline/descriptors/push constants,
    // while firstIndex/baseVertex live in VkDrawIndexedIndirectCommand. Keep four
    // immutable slices so near/far cascades and the two in-flight frame slots
    // never overwrite arguments that the GPU has not consumed yet.
    const bool useMegaShadow = (megaVB_ && megaIB_);
    const bool useIndirectShadow = useMegaShadow && indirectBuffer_ && indirectMapped_;
    const uint32_t frameSlot = vkCtx ? (vkCtx->getCurrentFrame() & 1u) : 0u;
    const uint32_t passSlot = std::min(shadowPassIndex, 1u);
    const uint32_t sliceIndex = frameSlot * 2u + passSlot;
    const VkDeviceSize sliceBytes = static_cast<VkDeviceSize>(MAX_INDIRECT_DRAWS) *
                                    sizeof(VkDrawIndexedIndirectCommand);
    const VkDeviceSize sliceOffset = static_cast<VkDeviceSize>(sliceIndex) * sliceBytes;
    auto* indirect = useIndirectShadow
        ? static_cast<VkDrawIndexedIndirectCommand*>(indirectMapped_) +
          static_cast<size_t>(sliceIndex) * MAX_INDIRECT_DRAWS
        : nullptr;
    uint32_t indirectCount = 0;
    uint32_t fallbackDraws = 0;
    uint32_t visibleMega = 0;

    // Fallback chunks can be submitted immediately; depth-only shadow ordering
    // is irrelevant. Mega chunks are gathered and emitted together afterward.
    for (const auto& chunk : chunks) {
        if (!chunk.isValid()) continue;
        if (!lightFrustum.intersectsSphere(chunk.boundingSphereCenter,
                                           chunk.boundingSphereRadius)) continue;
        if (receiverHull && !receiverHull->intersects(chunk.boundingSphereCenter,
                                                       chunk.boundingSphereRadius)) continue;

        if (useMegaShadow && chunk.megaBaseVertex >= 0) {
            ++visibleMega;
            if (indirect && indirectCount < MAX_INDIRECT_DRAWS) {
                auto& draw = indirect[indirectCount++];
                draw.indexCount = chunk.indexCount;
                draw.instanceCount = 1;
                draw.firstIndex = chunk.megaFirstIndex;
                draw.vertexOffset = chunk.megaBaseVertex;
                draw.firstInstance = 0;
                continue;
            }
            VkDeviceSize megaOffset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &megaVB_, &megaOffset);
            vkCmdBindIndexBuffer(cmd, megaIB_, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, chunk.indexCount, 1, chunk.megaFirstIndex,
                             chunk.megaBaseVertex, 0);
            ++fallbackDraws;
            continue;
        }

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &chunk.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, chunk.indexCount, 1, 0, 0, 0);
        ++fallbackDraws;
    }

    if (indirectCount) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(indirectCount) *
                                   sizeof(VkDrawIndexedIndirectCommand);
        const VkResult flush = vmaFlushAllocation(vkCtx->getAllocator(), indirectAlloc_,
                                                   sliceOffset, bytes);
        VkDeviceSize megaOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &megaVB_, &megaOffset);
        vkCmdBindIndexBuffer(cmd, megaIB_, 0, VK_INDEX_TYPE_UINT32);
        if (flush == VK_SUCCESS) {
            vkCmdDrawIndexedIndirect(cmd, indirectBuffer_, sliceOffset, indirectCount,
                                     sizeof(VkDrawIndexedIndirectCommand));
        } else {
            // Preserve exact coverage if a host-visible allocation ever reports
            // a flush failure; direct draws use the same gathered arguments.
            for (uint32_t i = 0; i < indirectCount; ++i) {
                const auto& draw = indirect[i];
                vkCmdDrawIndexed(cmd, draw.indexCount, draw.instanceCount, draw.firstIndex,
                                 draw.vertexOffset, draw.firstInstance);
                ++fallbackDraws;
            }
        }
    }

    static uint32_t reportFrame[2] = {};
    if ((++reportFrame[passSlot] % 300u) == 1u)
        LOG_INFO("[TERRAIN_SHADOW_BATCH] pass=", passSlot,
                 " visibleMega=", visibleMega, " indirectDraws=", indirectCount,
                 " apiSubmits=", indirectCount ? 1 : 0, " fallbackDraws=", fallbackDraws);

}

void TerrainRenderer::removeTile(int tileX, int tileY) {
    int removed = 0;
    auto it = chunks.begin();
    while (it != chunks.end()) {
        if (it->tileX == tileX && it->tileY == tileY) {
            destroyChunkGPU(*it);
            it = chunks.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    // Also collect masks from failed partial uploads with no published chunk.
    cleanupUnusedAlphaTextures();
    if (removed > 0) {
        LOG_DEBUG("Removed ", removed, " terrain chunks for tile [", tileX, ",", tileY, "]");
    }
}

void TerrainRenderer::cleanupUnusedAlphaTextures() {
    if (!vkCtx || textureCache.empty()) return;
    // Existing frame fences can predate a just-submitted upload. Prove its
    // completion independently before relying on frame fences for readers.
    // Never block travel: periodic cache cleanup retries after streaming.
    vkCtx->pollUploadBatches();
    if (!vkCtx->uploadsIdle()) return;
    // Alpha masks are created synchronously while binding a chunk; unlike
    // preloaded diffuse textures, no worker/finalizer owns an unbound alpha
    // image. All published references are in chunks, including partial tiles.
    // Keep shared masks until the final resident chunk releases its pointer.
    std::unordered_set<VkTexture*> referenced;
    for (const auto& chunk : chunks) {
        referenced.insert(chunk.baseTexture);
        for (auto* texture : chunk.layerTextures) referenced.insert(texture);
        for (auto* texture : chunk.alphaTextures) referenced.insert(texture);
    }
    const auto orphanAlpha = [&](const auto& item) {
        return item.first.compare(0, 8, "__alpha_") == 0 &&
               !referenced.count(item.second.texture.get());
    };
    const size_t orphanCount = std::count_if(textureCache.begin(),textureCache.end(),orphanAlpha);
    if (!orphanCount) return;
    auto retired = std::make_shared<std::vector<TextureCacheEntry>>();
    retired->reserve(orphanCount);
    const auto device = vkCtx->getDevice();
    const auto allocator = vkCtx->getAllocator();
    // Queue successfully before transferring ownership. Old chunk
    // descriptors may still be referenced by submitted graphics frames.
    // No wait-idle or per-texture fence is introduced by this reclamation.
    vkCtx->deferAfterAllFrameFences([retired,device,allocator]() {
        for (auto& entry : *retired)
            if (entry.texture) entry.texture->destroy(device,allocator);
    });
    alphaReuseCache_.forgetIf([&](VkTexture* texture) { return !referenced.count(texture); });
    size_t released = 0;
    for (auto it = textureCache.begin(); it != textureCache.end();) {
        if (!orphanAlpha(*it)) { ++it; continue; }
        released += it->second.approxBytes;
        retired->push_back(std::move(it->second));
        it = textureCache.erase(it);
    }
    textureCacheBytes_ -= std::min(textureCacheBytes_,released);
    LOG_INFO("[STREAM_RECLAIM] terrain orphan alpha images=",orphanCount,
             " bytes=",released," residentTextureBytes=",textureCacheBytes_);
}

void TerrainRenderer::clear() {
    if (!vkCtx) return;

    for (auto& chunk : chunks) {
        destroyChunkGPU(chunk);
    }
    chunks.clear();
    renderedChunks = 0;

    // A whole-world reset must not retain the previous map's terrain textures
    // or failed-load keys. Keep images alive until submitted frames finish,
    // just like the chunk buffers above; streaming individual chunks does not
    // call clear() and therefore still benefits from the texture cache.
    if (vkCtx && !textureCache.empty()) {
        auto textures = std::make_shared<decltype(textureCache)>(std::move(textureCache));
        const auto device = vkCtx->getDevice();
        const auto allocator = vkCtx->getAllocator();
        vkCtx->deferAfterAllFrameFences(
            [textures = std::move(textures), device, allocator]() mutable {
                for (auto& [path, entry] : *textures) {
                    if (entry.texture) entry.texture->destroy(device, allocator);
                }
            });
    }
    alphaReuseCache_.clear();
    alphaLookupCount_ = alphaOpaqueHits_ = 0;
    textureCache.clear();
    textureCacheBytes_ = 0;
    textureCacheCounter_ = 0;
    failedTextureCache_.clear();
    loggedTextureLoadFails_.clear();
    textureBudgetRejectWarnings_ = 0;
}

void TerrainRenderer::destroyChunkGPU(TerrainChunkGPU& chunk) {
    if (!vkCtx) return;

    VkDevice device = vkCtx->getDevice();
    VmaAllocator allocator = vkCtx->getAllocator();

    // These resources may still be referenced by in-flight command buffers from
    // previous frames. Defer actual destruction until this frame slot is safe.
    ::VkBuffer vertexBuffer = chunk.vertexBuffer;
    VmaAllocation vertexAlloc = chunk.vertexAlloc;
    ::VkBuffer indexBuffer = chunk.indexBuffer;
    VmaAllocation indexAlloc = chunk.indexAlloc;
    ::VkBuffer paramsUBO = chunk.paramsUBO;
    VmaAllocation paramsAlloc = chunk.paramsAlloc;
    VkDescriptorPool pool = materialDescPool;
    VkDescriptorSet materialSet = chunk.materialSet;

#ifdef WOWEE_PS4
    VkTexture* alphaPackTexture = chunk.alphaPackTexture.release();
    std::array<VkImageView, 3> alphaPackViews = {
        chunk.alphaPackViews[0], chunk.alphaPackViews[1], chunk.alphaPackViews[2]
    };
    for (auto& view : chunk.alphaPackViews) view = VK_NULL_HANDLE;
    chunk.alphaPackSampler = VK_NULL_HANDLE;
#endif

    std::vector<VkTexture*> alphaTextures;
    alphaTextures.reserve(chunk.ownedAlphaTextures.size());
    for (auto& tex : chunk.ownedAlphaTextures) {
        alphaTextures.push_back(tex.release());
    }

    chunk.vertexBuffer = VK_NULL_HANDLE;
    chunk.vertexAlloc = VK_NULL_HANDLE;
    chunk.indexBuffer = VK_NULL_HANDLE;
    chunk.indexAlloc = VK_NULL_HANDLE;
    chunk.paramsUBO = VK_NULL_HANDLE;
    chunk.paramsAlloc = VK_NULL_HANDLE;
    chunk.materialSet = VK_NULL_HANDLE;
    chunk.ownedAlphaTextures.clear();

    vkCtx->deferAfterAllFrameFences([device, allocator, vertexBuffer, vertexAlloc, indexBuffer, indexAlloc,
                                     paramsUBO, paramsAlloc, pool, materialSet, alphaTextures
#ifdef WOWEE_PS4
                                     , alphaPackTexture, alphaPackViews
#endif
                                     ]() {
        if (vertexBuffer) {
            AllocatedBuffer ab{}; ab.buffer = vertexBuffer; ab.allocation = vertexAlloc;
            destroyBuffer(allocator, ab);
        }
        if (indexBuffer) {
            AllocatedBuffer ab{}; ab.buffer = indexBuffer; ab.allocation = indexAlloc;
            destroyBuffer(allocator, ab);
        }
        if (paramsUBO) {
            AllocatedBuffer ab{}; ab.buffer = paramsUBO; ab.allocation = paramsAlloc;
            destroyBuffer(allocator, ab);
        }
        if (materialSet && pool) {
            VkDescriptorSet set = materialSet;
            vkFreeDescriptorSets(device, pool, 1, &set);
        }
        for (VkTexture* tex : alphaTextures) {
            if (!tex) continue;
            tex->destroy(device, allocator);
            delete tex;
        }
#ifdef WOWEE_PS4
        for (VkImageView view : alphaPackViews)
            if (view) vkDestroyImageView(device, view, nullptr);
        if (alphaPackTexture) {
            alphaPackTexture->destroy(device, allocator);
            delete alphaPackTexture;
        }
#endif
    });
}

int TerrainRenderer::getTriangleCount() const {
    int total = 0;
    for (const auto& chunk : chunks) {
        total += chunk.indexCount / 3;
    }
    return total;
}

bool TerrainRenderer::isChunkVisible(const TerrainChunkGPU& chunk, const Frustum& frustum) {
    return frustum.intersectsSphere(chunk.boundingSphereCenter, chunk.boundingSphereRadius);
}

void TerrainRenderer::calculateBoundingSphere(TerrainChunkGPU& gpuChunk,
                                                const pipeline::ChunkMesh& meshChunk) {
    if (meshChunk.vertices.empty()) {
        gpuChunk.boundingSphereRadius = 0.0f;
        gpuChunk.boundingSphereCenter = glm::vec3(0.0f);
        return;
    }

    glm::vec3 min(std::numeric_limits<float>::max());
    glm::vec3 max(std::numeric_limits<float>::lowest());

    for (const auto& vertex : meshChunk.vertices) {
        glm::vec3 pos(vertex.position[0], vertex.position[1], vertex.position[2]);
        min = glm::min(min, pos);
        max = glm::max(max, pos);
    }

    gpuChunk.boundingSphereCenter = (min + max) * 0.5f;

    float maxDistSq = 0.0f;
    for (const auto& vertex : meshChunk.vertices) {
        glm::vec3 pos(vertex.position[0], vertex.position[1], vertex.position[2]);
        glm::vec3 diff = pos - gpuChunk.boundingSphereCenter;
        float distSq = glm::dot(diff, diff);
        maxDistSq = std::max(maxDistSq, distSq);
    }

    gpuChunk.boundingSphereRadius = std::sqrt(maxDistSq);
}

} // namespace rendering
} // namespace wowee
