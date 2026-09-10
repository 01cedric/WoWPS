#include "rendering/portrait_camera.hpp"
#include "rendering/preview_framing.hpp"
#include "rendering/character_preview.hpp"
#include "ui/unit_portrait.hpp"
#include "rendering/preview_equipment_selection.hpp"
#include "rendering/imgui_texture.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/animation/animation_ids.hpp"
#include "rendering/vk_render_target.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/camera.hpp"
#include "rendering/glue_camera.hpp"
#include "rendering/glue_scene_effects.hpp"
#include "rendering/renderer.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/char_sections.hpp"
#include "pipeline/dbc_layout.hpp"
#include "core/appearance_composer.hpp"
#include "core/geoset_rules.hpp"
#include "core/helm_visual.hpp"
#include "core/helm_visibility.hpp"
#include "pipeline/item_textures.hpp"
#include "pipeline/m2_asset_loader.hpp"
#include "core/logger.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#endif
#include <cstdlib>
#include <cstdio>
#include "core/application.hpp"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <cstring>

namespace wowee {
namespace rendering {

namespace {

bool isFiniteVec3(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void frameCameraForModelBounds(Camera& camera, const glm::vec3& boundMin, const glm::vec3& boundMax) {
    if (!isFiniteVec3(boundMin) || !isFiniteVec3(boundMax)) {
        return;
    }

    const glm::vec3 extent = boundMax - boundMin;
    if (extent.z <= 0.0f) {
        return;
    }

    const float fovY = glm::radians(camera.getFovDegrees());
    const float tanHalfY = std::tan(fovY * 0.5f);
    if (!std::isfinite(tanHalfY) || tanHalfY <= 0.0f) {
        return;
    }

    const float tanHalfX = tanHalfY * std::max(camera.getAspectRatio(), 0.1f);
    const float height = std::max(extent.z, 0.1f);
    const float width = std::max(std::max(extent.x, extent.y), 0.1f);
    const float margin = 1.50f;
    const float distanceForHeight = (height * margin) / (2.0f * tanHalfY);
    const float distanceForWidth = (width * margin) / (2.0f * tanHalfX);
    const float distance = std::max({4.5f, distanceForHeight, distanceForWidth});
    const float centerZ = (boundMin.z + boundMax.z) * 0.5f;

    camera.setPosition(glm::vec3(0.0f, distance, centerZ));
    camera.setRotation(270.0f, 0.0f);
}

} // namespace

CharacterPreview::CharacterPreview() = default;

CharacterPreview::~CharacterPreview() {
    shutdown();
}

void CharacterPreview::ensureAppearanceGeosetsLoaded() {
    if (appearanceGeosetsLoaded_ || !assetManager_) {
        return;
    }

    appearanceGeosetsLoaded_ = true;
    hairGeosetMap_.clear();
    facialHairGeosetMap_.clear();

    // CharHairGeosets.dbc maps (race, sex, hairStyleId) to the group-0
    // scalp/hair submesh. Activating every group-0 submesh draws all hair
    // variants at once, which shows up as flickering magenta patches.
    if (auto chg = assetManager_->loadDBC("CharHairGeosets.dbc"); chg && chg->isLoaded()) {
        const auto* chgL = pipeline::getActiveDBCLayout()
            ? pipeline::getActiveDBCLayout()->getLayout("CharHairGeosets") : nullptr;
        for (uint32_t i = 0; i < chg->getRecordCount(); i++) {
            uint32_t raceId = chg->getUInt32(i, chgL ? (*chgL)["RaceID"] : 1);
            uint32_t sexId = chg->getUInt32(i, chgL ? (*chgL)["SexID"] : 2);
            uint32_t variation = chg->getUInt32(i, chgL ? (*chgL)["Variation"] : 3);
            uint32_t geosetId = chg->getUInt32(i, chgL ? (*chgL)["GeosetID"] : 4);
            const bool useDefaultScalp = chg->getFieldCount() > 5 && chg->getUInt32(i, 5) != 0;
            const uint32_t key = core::appearanceKey(static_cast<uint8_t>(raceId),
                                                    static_cast<uint8_t>(sexId),
                                                    static_cast<uint8_t>(variation));
            hairGeosetMap_[key] = static_cast<uint16_t>(useDefaultScalp ? 1 : geosetId);
        }
        LOG_INFO("CharacterPreview: loaded ", hairGeosetMap_.size(), " hair geoset mappings");
    }

    if (auto cfh = assetManager_->loadDBC("CharacterFacialHairStyles.dbc"); cfh && cfh->isLoaded()) {
        const auto* cfhL = pipeline::getActiveDBCLayout()
            ? pipeline::getActiveDBCLayout()->getLayout("CharacterFacialHairStyles") : nullptr;
        const auto fhF = pipeline::detectFacialHairFields(cfh.get(), cfhL);
        for (uint32_t i = 0; i < cfh->getRecordCount(); i++) {
            uint32_t raceId = cfh->getUInt32(i, cfhL ? (*cfhL)["RaceID"] : 0);
            uint32_t sexId = cfh->getUInt32(i, cfhL ? (*cfhL)["SexID"] : 1);
            uint32_t variation = cfh->getUInt32(i, cfhL ? (*cfhL)["Variation"] : 2);
            const uint32_t key = core::appearanceKey(static_cast<uint8_t>(raceId),
                                                    static_cast<uint8_t>(sexId),
                                                    static_cast<uint8_t>(variation));

            FacialHairGeosets geosets;
            // Whichever columns this copy of the DBC keeps them in - see
            // detectFacialHairFields, and the same read in EntitySpawner.
            geosets.geoset100 = static_cast<uint16_t>(cfh->getUInt32(i, fhF.geoset100));
            geosets.geoset300 = static_cast<uint16_t>(cfh->getUInt32(i, fhF.geoset300));
            geosets.geoset200 = static_cast<uint16_t>(cfh->getUInt32(i, fhF.geoset200));
            facialHairGeosetMap_[key] = geosets;
        }
        LOG_INFO("CharacterPreview: loaded ", facialHairGeosetMap_.size(), " facial hair geoset mappings");
    }
}

uint16_t CharacterPreview::selectedHairScalpGeoset() const {
    const uint8_t raceId = static_cast<uint8_t>(race_);
    const uint8_t sexId = (gender_ == game::Gender::FEMALE ||
                           (gender_ == game::Gender::NONBINARY && useFemaleModel_)) ? 1u : 0u;
    const uint32_t key = core::appearanceKey(raceId, sexId, static_cast<uint8_t>(hairStyle_));

    auto it = hairGeosetMap_.find(key);
    if (it != hairGeosetMap_.end() && it->second > 0) {
        return it->second;
    }

    // Last-resort heuristic for incomplete data sets. The DBC path above is
    // expected for real clients and is much more reliable than this fallback.
    return static_cast<uint16_t>(std::max<uint8_t>(hairStyle_ + 1, 1));
}

std::unordered_set<uint16_t> CharacterPreview::buildBaseGeosets() {
    ensureAppearanceGeosetsLoaded();

    const uint8_t raceId = static_cast<uint8_t>(race_);
    const uint8_t sexId = (gender_ == game::Gender::FEMALE ||
                           (gender_ == game::Gender::NONBINARY && useFemaleModel_)) ? 1u : 0u;
    const uint16_t selectedHairScalp = selectedHairScalpGeoset();

    uint16_t facial100 = 1, facial200 = 1, facial300 = 1;
    auto itFacial = facialHairGeosetMap_.find(
        core::appearanceKey(raceId, sexId, static_cast<uint8_t>(facialHair_)));
    if (itFacial != facialHairGeosetMap_.end()) {
        facial100 = itFacial->second.geoset100;
        facial200 = itFacial->second.geoset200;
        facial300 = itFacial->second.geoset300;
    }

    // The same bare set the player gets. This used to be its own list and had
    // drifted: it named one of the two feet variants, so an HD model spelling
    // its feet the other way stood in the portrait without them, and it named
    // the no-cloak panel, which the HD models do not carry.
    std::unordered_set<uint16_t> activeGeosets =
        core::bareCharacterGeosets(selectedHairScalp, facial100, facial200, facial300, raceId);

    return activeGeosets;
}

bool CharacterPreview::initialize(pipeline::AssetManager* am, int width, int height) {
    assetManager_ = am;
    // If already initialized with valid resources, reuse them.
    // This avoids destroying GPU resources that may still be referenced by
    // an in-flight command buffer (compositePass recorded earlier this frame).
    if (renderTarget_ && renderTarget_->isValid() && charRenderer_ && camera_ &&
        imguiTextureId_ && previewUBOMapped_[0] && previewUBOMapped_[1] &&
        (width <= 0 || width == fboWidth_) && (height <= 0 || height == fboHeight_)) {
        return true;
    }
    if (renderTarget_ || charRenderer_) shutdown();
    lastError_.clear();

    // Only where the view is actually being built. The render target, the
    // camera's aspect ratio and the composite are all sized from these, so
    // taking a new size while keeping a target built at the old one would put
    // every one of the three out of step with the image they are drawing into.
    if (width > 0) fboWidth_ = width;
    if (height > 0) fboHeight_ = height;

    auto* appRenderer = core::Application::getInstance().getRenderer();
    vkCtx_ = appRenderer ? appRenderer->getVkContext() : nullptr;
    VkDescriptorSetLayout perFrameLayout = appRenderer ? appRenderer->getPerFrameSetLayout() : VK_NULL_HANDLE;

    if (!vkCtx_ || perFrameLayout == VK_NULL_HANDLE) {
        return failPreview("3D scene renderer is unavailable (Vulkan context/layout).");
    }

    // Create off-screen render target first (need its render pass for pipeline creation)
    if (!createFBO()) {
        destroyFBO();
        return failPreview("Could not allocate all 3D scene targets, descriptors and camera buffers.");
    }

    // Initialize CharacterRenderer with our off-screen render pass
    charRenderer_ = std::make_unique<CharacterRenderer>();
    if (!charRenderer_->initialize(vkCtx_, perFrameLayout, am, renderTarget_->getRenderPass(),
                                   renderTarget_->getSampleCount())) {
        shutdown();
        return failPreview("Could not initialize the 3D character renderer.");
    }

    // Configure lighting for character preview
    // Use distant fog to avoid clipping, enable shadows for visual depth
    charRenderer_->setFog(glm::vec3(0.05f, 0.05f, 0.1f), 9999.0f, 10000.0f);

    camera_ = std::make_unique<Camera>();
    // Portrait-style camera: WoW Z-up coordinate system
    // Model at origin, camera positioned along +Y looking toward -Y
    camera_->setFov(30.0f);
    camera_->setAspectRatio(static_cast<float>(fboWidth_) / static_cast<float>(fboHeight_));
    // Pull camera back far enough to see full body + head with margin
    camera_->setPosition(glm::vec3(0.0f, 4.5f, 0.9f));
    camera_->setRotation(270.0f, 0.0f);

    LOG_INFO("CharacterPreview initialized (", fboWidth_, "x", fboHeight_, ")");
    return true;
}

void CharacterPreview::shutdown() {
    // Unregister from renderer before destroying resources
    auto* appRenderer = core::Application::getInstance().getRenderer();
    if (appRenderer) appRenderer->unregisterPreview(this);

    // Scene teardown is called before frame recording. Retire the submitted
    // frames before destroying the offscreen texture and its descriptors.
    if (vkCtx_ && vkCtx_->getDevice()) vkDeviceWaitIdle(vkCtx_->getDevice());
    sceneEffects_.reset();

    if (charRenderer_) {
        charRenderer_->shutdown();
        charRenderer_.reset();
    }
    camera_.reset();
    destroyFBO();
    modelLoaded_ = false;
    compositeRendered_ = false;
    instanceId_ = 0;
    backdropInstanceId_ = 0;
    backdropRace_ = -1;
    hasSceneCamera_ = false;
    compositeRequested_ = false;
}

bool CharacterPreview::failPreview(const std::string& message) {
    if(lastError_==message)return false;
    lastError_ = message;
    LOG_ERROR("CharacterPreview: ", message);
    return false;
}

void CharacterPreview::loadSceneEffects(const pipeline::M2Model& scene) {
    if (!sceneEffects_) sceneEffects_ = std::make_unique<GlueSceneEffects>();
    auto* renderer = core::Application::getInstance().getRenderer();
    if (!renderer || !renderTarget_ || !sceneEffects_->load(scene, vkCtx_,
        renderer->getPerFrameSetLayout(), assetManager_, renderTarget_->getRenderPass(),
        renderTarget_->getSampleCount())) {
        failPreview("Could not initialize the authored 3D scene particle/ribbon effects.");
    }
}

bool CharacterPreview::createFBO() {
    if (!vkCtx_) return false;
    VkDevice device = vkCtx_->getDevice();
    VmaAllocator allocator = vkCtx_->getAllocator();

    // 1. Create off-screen render target with depth.
    //
    // 4x MSAA where the device offers it; a device whose framebuffers only
    // take fewer samples (or one, as on the console) gets the most it can
    // rather than a create call that fails.
    VkSampleCountFlagBits previewSamples = VK_SAMPLE_COUNT_4_BIT;
    if (const VkSampleCountFlagBits maxSamples = vkCtx_->getMaxUsableSampleCount();
        maxSamples < previewSamples) {
        previewSamples = maxSamples;
    }
    renderTarget_ = std::make_unique<VkRenderTarget>();
    if (!renderTarget_->create(*vkCtx_, fboWidth_, fboHeight_, VK_FORMAT_R8G8B8A8_UNORM, true,
                               previewSamples)) {
        LOG_ERROR("CharacterPreview: failed to create render target");
        renderTarget_.reset();
        return false;
    }

    // 1b. Transition the color image from UNDEFINED to SHADER_READ_ONLY_OPTIMAL
    // so that ImGui::Image doesn't sample an image in UNDEFINED layout before
    // the first compositePass runs.
    {
        VkCommandBuffer cmd = vkCtx_->beginSingleTimeCommands();
        if (!cmd) return false;
        VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = renderTarget_->getColorImage();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkDependencyInfo barrierDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        barrierDep.dependencyFlags = 0;
        barrierDep.imageMemoryBarrierCount = 1;
        barrierDep.pImageMemoryBarriers = &barrier;
        cmdPipelineBarrier2(cmd, barrierDep);
        vkCtx_->endSingleTimeCommands(cmd);
    }

#ifndef WOWEE_PS4
    // 2. Create 1x1 dummy depth texture (shadow map placeholder, depth=1.0 = no shadow).
    //    Must be a depth format for sampler2DShadow compatibility.
    {
        VkImageCreateInfo imgCI{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imgCI.imageType = VK_IMAGE_TYPE_2D;
        imgCI.format = VK_FORMAT_D16_UNORM;
        imgCI.extent = {.width = 1, .height = 1, .depth = 1};
        imgCI.mipLevels = 1;
        imgCI.arrayLayers = 1;
        imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocCI{};
        allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(vkCtx_->getAllocator(), &imgCI, &allocCI,
                &dummyShadowImage_, &dummyShadowAlloc_, nullptr) != VK_SUCCESS) {
            LOG_ERROR("CharacterPreview: failed to create dummy shadow image");
            return false;
        }
        VkImageViewCreateInfo viewCI{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image = dummyShadowImage_;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = VK_FORMAT_D16_UNORM;
        viewCI.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
        if (vkCreateImageView(device, &viewCI, nullptr, &dummyShadowView_) != VK_SUCCESS) {
            LOG_ERROR("CharacterPreview: failed to create dummy shadow image view");
            return false;
        }
        // Clear to depth 1.0 and transition to shader-read layout
        vkCtx_->immediateSubmit([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier2 toTransfer{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toTransfer.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            toTransfer.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            toTransfer.image = dummyShadowImage_;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            VkDependencyInfo toTransferDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            toTransferDep.dependencyFlags = 0;
            toTransferDep.imageMemoryBarrierCount = 1;
            toTransferDep.pImageMemoryBarriers = &toTransfer;
            cmdPipelineBarrier2(cmd, toTransferDep);
            VkClearDepthStencilValue clearVal{.depth = 1.0f, .stencil = 0};
            VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
            vkCmdClearDepthStencilImage(cmd, dummyShadowImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearVal, 1, &range);
            VkImageMemoryBarrier2 toRead{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toRead.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            toRead.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            toRead.image = dummyShadowImage_;
            toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
            toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            VkDependencyInfo toReadDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            toReadDep.dependencyFlags = 0;
            toReadDep.imageMemoryBarrierCount = 1;
            toReadDep.pImageMemoryBarriers = &toRead;
            cmdPipelineBarrier2(cmd, toReadDep);
        });
    }

#else
    // 2. The console's shadow placeholder: a white 1x1 colour texture read
    //    through the comparison sampler. A sampled depth image is a path the
    //    ps4_vulkan ICD has not proven, and this preview never shadows.
    ps4ShadowPlaceholder_ = std::make_unique<VkTexture>();
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        if (!ps4ShadowPlaceholder_->upload(*vkCtx_, white, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false)) {
            LOG_ERROR("CharacterPreview: shadow placeholder texture failed");
            ps4ShadowPlaceholder_.reset();
            return false;
        }
    }
#endif
    // 3. Create descriptor pool for per-frame sets (2 UBO + 2 sampler)
    {
        VkDescriptorPoolSize sizes[2]{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = MAX_FRAMES;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = MAX_FRAMES;

        VkDescriptorPoolCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.maxSets = MAX_FRAMES;
        ci.poolSizeCount = 2;
        ci.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(device, &ci, nullptr, &previewDescPool_) != VK_SUCCESS) {
            LOG_ERROR("CharacterPreview: failed to create descriptor pool");
            return false;
        }
    }

    // 4. Create per-frame UBOs and descriptor sets
    auto* appRenderer = core::Application::getInstance().getRenderer();
    VkDescriptorSetLayout perFrameLayout = appRenderer->getPerFrameSetLayout();

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        // Create mapped UBO
        VkBufferCreateInfo bufInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufInfo.size = sizeof(GPUPerFrameData);
        bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo mapInfo{};
        if (vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                &previewUBO_[i], &previewUBOAlloc_[i], &mapInfo) != VK_SUCCESS) {
            LOG_ERROR("CharacterPreview: failed to create UBO ", i);
            return false;
        }
        previewUBOMapped_[i] = mapInfo.pMappedData;
        if (!previewUBOMapped_[i]) return false;

        // Allocate descriptor set
        VkDescriptorSetAllocateInfo setAlloc{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setAlloc.descriptorPool = previewDescPool_;
        setAlloc.descriptorSetCount = 1;
        setAlloc.pSetLayouts = &perFrameLayout;
        if (vkAllocateDescriptorSets(device, &setAlloc, &previewPerFrameSet_[i]) != VK_SUCCESS) {
            LOG_ERROR("CharacterPreview: failed to allocate descriptor set ", i);
            return false;
        }

        // Write UBO binding (0) and shadow sampler binding (1) using dummy white texture
        VkDescriptorBufferInfo descBuf{};
        descBuf.buffer = previewUBO_[i];
        descBuf.offset = 0;
        descBuf.range = sizeof(GPUPerFrameData);

        VkDescriptorImageInfo shadowImg{};
        // sampler is ignored on every platform but PS4: this set comes from
        // the renderer's per-frame layout, where binding 1 declares an
        // immutable comparison sampler - except on PS4, which has no
        // immutable samplers (see Renderer::createPerFrameResources) and
        // needs the real handle written into every descriptor set explicitly.
#ifdef WOWEE_PS4
        shadowImg.imageView = ps4ShadowPlaceholder_ ? ps4ShadowPlaceholder_->descriptorInfo().imageView
                                                    : VK_NULL_HANDLE;
#else
        shadowImg.imageView = dummyShadowView_;
#endif
        shadowImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#if defined(__ORBIS__) || defined(PS4) || defined(WOWEE_PS4)
        shadowImg.sampler = appRenderer->getShadowSampler();
#endif

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = previewPerFrameSet_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &descBuf;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = previewPerFrameSet_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &shadowImg;

        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

    // 5. Register the color attachment as an ImGui texture
    imguiTextureId_ = ImGui_ImplVulkan_AddTexture(
        renderTarget_->getSampler(),
        renderTarget_->getColorImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!imguiTextureId_) return false;
    LOG_INFO("CharacterPreview: off-screen FBO created (", fboWidth_, "x", fboHeight_, ")");
    return true;
}

void CharacterPreview::destroyFBO() {
    ps4ShadowPlaceholder_.reset();
    if (!vkCtx_) return;
    VkDevice device = vkCtx_->getDevice();
    VmaAllocator allocator = vkCtx_->getAllocator();

    if (imguiTextureId_) {
        removeImGuiTexture(imguiTextureId_);
        imguiTextureId_ = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        destroy(allocator, previewUBO_[i], previewUBOAlloc_[i]);
        previewUBOMapped_[i] = nullptr;
        previewPerFrameSet_[i] = VK_NULL_HANDLE;
    }

    destroy(device, previewDescPool_);

    if (dummyShadowView_) { vkDestroyImageView(device, dummyShadowView_, nullptr); dummyShadowView_ = VK_NULL_HANDLE; }
    if (dummyShadowImage_) { vmaDestroyImage(allocator, dummyShadowImage_, dummyShadowAlloc_); dummyShadowImage_ = VK_NULL_HANDLE; dummyShadowAlloc_ = VK_NULL_HANDLE; }

    if (renderTarget_) {
        renderTarget_->destroy(device, allocator);
        renderTarget_.reset();
    }
}

void CharacterPreview::releaseCharacterModel() {
    compositeRendered_ = false;
    modelLoaded_ = false;
    if (!charRenderer_) return;
    if (instanceId_) {
        charRenderer_->removeInstance(instanceId_);
        instanceId_ = 0;
    }
    // The old decoded model used to survive until loadModel(new) after the
    // new M2, skin and ALL animations had already been allocated. A target
    // change could therefore require three copies at once on a nearly full
    // flexible heap. GPU buffers retire behind fences; CPU tracks can go now.
    charRenderer_->unloadModelIfUnused(PREVIEW_MODEL_ID);
    charRenderer_->reclaimUnusedResources();
}

bool CharacterPreview::loadCharacter(game::Race race, game::Gender gender,
                                      uint8_t skin, uint8_t face,
                                      uint8_t hairStyle, uint8_t hairColor,
                                      uint8_t facialHair, bool useFemaleModel) {
    if (!charRenderer_ || !assetManager_ || !assetManager_->isInitialized()) {
        return false;
    }

    releaseCharacterModel();

    std::string m2Path = game::getPlayerModelPath(race, gender, useFemaleModel);

    // Look up CharSections.dbc for all appearance textures
    uint32_t targetRaceId = static_cast<uint32_t>(race);
    uint32_t targetSexId = (gender == game::Gender::FEMALE ||
                            (gender == game::Gender::NONBINARY && useFemaleModel)) ? 1u : 0u;

    std::string faceLowerPath;
    std::string faceUpperPath;
    std::string hairScalpPath;
    std::vector<std::string> underwearPaths;
    bodySkinPath_.clear();
    skinExtraPath_.clear();
    baseLayers_.clear();

    auto charSectionsDbc = assetManager_->loadDBC("CharSections.dbc");
    if (charSectionsDbc) {
        const auto* csL = pipeline::getActiveDBCLayout()
            ? pipeline::getActiveDBCLayout()->getLayout("CharSections") : nullptr;
        const auto csF = pipeline::detectCharSectionsFields(charSectionsDbc.get(), csL);

        // The one reader, in pipeline/char_sections.hpp. This copy had no
        // fallback for a face the table does not carry - a character created
        // with a face number that has no row simply had no face - and it
        // matched the skin and underwear rows on variation 0, which the other
        // readers do not, so a table that numbers those rows any other way
        // found nothing here and everything there.
        pipeline::CharacterAppearance who;
        who.raceId = targetRaceId;
        who.sexId = targetSexId;
        who.skinId = static_cast<uint8_t>(skin);
        who.faceId = static_cast<uint8_t>(face);
        who.hairStyleId = static_cast<uint8_t>(hairStyle);
        who.hairColorId = static_cast<uint8_t>(hairColor);

        const auto sections = pipeline::resolveCharacterSections(
            charSectionsDbc.get(), csF, who,
            [](const std::string& path, void* ctx) {
                return static_cast<pipeline::AssetManager*>(ctx)->fileExists(path);
            },
            assetManager_);

        bodySkinPath_   = sections.bodySkin;
        skinExtraPath_  = sections.skinExtra;
        faceLowerPath   = sections.faceLower;
        faceUpperPath   = sections.faceUpper;
        hairScalpPath   = sections.hair;
        underwearPaths  = sections.underwear;

        LOG_INFO("CharSections lookup: skin=",
                 bodySkinPath_.empty() ? "(not found)" : bodySkinPath_,
                 " face=", sections.haveFace
                     ? (sections.exactFace ? faceLowerPath : faceLowerPath + " (nearest)")
                     : "(not found)",
                 " hair=", sections.haveHair ? hairScalpPath : "(not found)",
                 " underwear=", underwearPaths.size(), " textures");
    } else {
        LOG_WARNING("CharSections.dbc not loaded - no character textures");
    }

    // Only CPU model data is shared. Composites, geosets and GPU state stay private.
    pipeline::CharacterSectionTextures resolved;
    resolved.bodySkin=bodySkinPath_;resolved.skinExtra=skinExtraPath_;
    resolved.faceLower=faceLowerPath;resolved.faceUpper=faceUpperPath;
    resolved.hair=hairScalpPath;resolved.underwear=underwearPaths;
    const size_t first=m2Path.find('\\');
    const size_t second=first==std::string::npos?std::string::npos:m2Path.find('\\',first+1);
    const std::string raceFolder=second==std::string::npos?std::string{}:m2Path.substr(first+1,second-first-1);
    auto modelAsset=pipeline::loadCharacterPreviewModel(*assetManager_,m2Path,resolved,raceFolder);
    if (!modelAsset) {
        LOG_WARNING("CharacterPreview: failed to prepare model: ",m2Path);
        return false;
    }
    const auto& model=*modelAsset;

    if (camera_) {
        glm::vec3 frameMin = model.boundMin;
        glm::vec3 frameMax = model.boundMax;
        if (!model.vertices.empty()) {
            glm::vec3 tightMin(std::numeric_limits<float>::max());
            glm::vec3 tightMax(-std::numeric_limits<float>::max());
            for (const auto& v : model.vertices) {
                if (!isFiniteVec3(v.position)) continue;
                tightMin = glm::min(tightMin, v.position);
                tightMax = glm::max(tightMax, v.position);
            }
            if (tightMin.x <= tightMax.x && tightMin.y <= tightMax.y && tightMin.z <= tightMax.z) {
                frameMin = tightMin;
                frameMax = tightMax;
            }
        }
        frameCameraForModelBounds(*camera_, frameMin, frameMax);
        modelBoundMinZ_ = frameMin.z;
        modelBoundMaxZ_ = frameMax.z;
        readPortraitFraming(model);
        modelBoundMin_=frameMin; modelBoundMax_=frameMax; previewFitScale_=-1;
        fullBodyDistance_ = camera_->getPosition().y;
    }

    if (!charRenderer_->loadSharedModel(modelAsset, PREVIEW_MODEL_ID)) {
        LOG_WARNING("CharacterPreview: failed to load model to GPU");
        return false;
    }
    // Composite body skin + face + underwear overlays
    if (!bodySkinPath_.empty()) {
        std::vector<std::string> layers;
        layers.push_back(bodySkinPath_);
        // Face lower texture composited onto body at the face region
        if (!faceLowerPath.empty()) {
            layers.push_back(faceLowerPath);
        }
        if (!faceUpperPath.empty()) {
            layers.push_back(faceUpperPath);
        }
        for (const auto& up : underwearPaths) {
            layers.push_back(up);
        }

        // Cache for later equipment compositing.
        // Keep baseLayers_ without the base skin (compositeWithRegions takes basePath separately).
        if (!faceLowerPath.empty()) baseLayers_.push_back(faceLowerPath);
        if (!faceUpperPath.empty()) baseLayers_.push_back(faceUpperPath);
        for (const auto& up : underwearPaths) baseLayers_.push_back(up);

        if (layers.size() > 1) {
            VkTexture* compositeTex = charRenderer_->compositeTextures(layers);
            if (compositeTex != nullptr) {
                for (size_t ti = 0; ti < model.textures.size(); ti++) {
                    if (model.textures[ti].type == 1) {
                        charRenderer_->setModelTexture(PREVIEW_MODEL_ID, static_cast<uint32_t>(ti), compositeTex);
                        break;
                    }
                }
            }
        } else {
            // Single layer (body skin only, no face/underwear overlays) - load directly
            VkTexture* skinTex = charRenderer_->loadTexture(bodySkinPath_);
            if (skinTex != nullptr) {
                for (size_t ti = 0; ti < model.textures.size(); ti++) {
                    if (model.textures[ti].type == 1) {
                        charRenderer_->setModelTexture(PREVIEW_MODEL_ID, static_cast<uint32_t>(ti), skinTex);
                        break;
                    }
                }
            }
        }
    }

    // If hair scalp texture was found, ensure it's loaded for type-6 slot
    if (!hairScalpPath.empty()) {
        VkTexture* hairTex = charRenderer_->loadTexture(hairScalpPath);
        if (hairTex != nullptr) {
            for (size_t ti = 0; ti < model.textures.size(); ti++) {
                if (model.textures[ti].type == 6) {
                    charRenderer_->setModelTexture(PREVIEW_MODEL_ID, static_cast<uint32_t>(ti), hairTex);
                    break;
                }
            }
        }
    }

    // Create instance at origin with current yaw
    instanceId_ = charRenderer_->createInstance(PREVIEW_MODEL_ID,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glue::previewModelRotation(modelYaw_),
        1.0f);

    if (instanceId_ == 0) {
        LOG_WARNING("CharacterPreview: failed to create instance");
        return false;
    }

    // Cache core appearance before geoset selection; the DBC resolver uses it.
    race_ = race;
    gender_ = gender;
    useFemaleModel_ = useFemaleModel;
    hairStyle_ = hairStyle;
    facialHair_ = facialHair;

    std::unordered_set<uint16_t> activeGeosets = buildBaseGeosets();
    charRenderer_->setActiveGeosets(instanceId_, activeGeosets);

    // Play idle animation (Stand = animation ID 0)
    charRenderer_->playAnimation(instanceId_, rendering::anim::STAND, true);

    // Cache the type-1 texture slot index so applyEquipment can update it.
    skinTextureSlotIndex_ = 0;
    for (size_t ti = 0; ti < model.textures.size(); ti++) {
        if (model.textures[ti].type == 1) {
            skinTextureSlotIndex_ = static_cast<uint32_t>(ti);
            break;
        }
    }

    modelLoaded_ = true;
    compositeRendered_ = false;
    lastError_.clear();
    loadRacialBackdrop(race);
    LOG_INFO("CharacterPreview: loaded ", m2Path,
             " skin=", static_cast<int>(skin), " face=", static_cast<int>(face),
             " hair=", static_cast<int>(hairStyle), " hairColor=", static_cast<int>(hairColor),
             " facial=", static_cast<int>(facialHair));
    return true;
}

bool CharacterPreview::applyEquipment(const std::vector<game::EquipmentItem>& equipment) {
    if (!modelLoaded_ || instanceId_ == 0 || !charRenderer_ || !assetManager_ || !assetManager_->isInitialized()) {
        return false;
    }

    // Weapons first, and unconditionally: they depend on nothing below, while the
    // geoset/skin work that follows bails out early on characters whose body skin
    // could not be composited. Attaching last meant those characters showed no
    // weapon at all - and kept the previously selected character's weapon and
    // enchant, since detaching happens here too.
    attachWeapons(equipment);
    const uint32_t attachedHelmDisplayId = attachHelm(equipment);

    charRenderer_->clearTextureSlotOverride(instanceId_, static_cast<uint16_t>(skinTextureSlotIndex_));
    charRenderer_->setGroupTextureOverride(instanceId_, 15, nullptr);

    auto displayInfoDbc = assetManager_->loadDBC("ItemDisplayInfo.dbc");
    if (!displayInfoDbc || !displayInfoDbc->isLoaded()) {
        // Equipment removal must restore hair even if its display table is missing.
        charRenderer_->setActiveGeosets(instanceId_, buildBaseGeosets());
        LOG_WARNING("applyEquipment: ItemDisplayInfo.dbc not loaded");
        return false;
    }

    auto hasInvType = [&](std::initializer_list<uint8_t> types) -> bool {
        for (const auto& it : equipment) {
            if (it.displayModel == 0) continue;
            for (uint8_t t : types) {
                if (it.inventoryType == t) return true;
            }
        }
        return false;
    };

    auto findDisplayId = [&](std::initializer_list<uint8_t> types) -> uint32_t {
        for (const auto& it : equipment) {
            if (it.displayModel == 0) continue;
            for (uint8_t t : types) {
                if (it.inventoryType == t) return it.displayModel;
            }
        }
        return 0;
    };

    const auto* idiL = pipeline::getActiveDBCLayout()
        ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
    const uint32_t geosetGroup1Field = idiL ? (*idiL)["GeosetGroup1"] : 7u;
    const uint32_t geosetGroup3Field = idiL ? (*idiL)["GeosetGroup3"] : 9u;

    auto getGeosetGroup = [&](uint32_t displayInfoId, uint32_t fieldIdx) -> uint32_t {
        if (displayInfoId == 0) return 0;
        int32_t recIdx = displayInfoDbc->findRecordById(displayInfoId);
        if (recIdx < 0) return 0;
        return displayInfoDbc->getUInt32(static_cast<uint32_t>(recIdx), fieldIdx);
    };

    // --- Geosets ---
    // M2 geoset IDs encode body part group × 100 + variant (e.g., 801 = group 8
    // (sleeves) variant 1, 1301 = group 13 (pants) variant 1). ItemDisplayInfo.dbc
    // provides the variant offset per equipped item; base IDs are per-group constants.
    std::unordered_set<uint16_t> geosets = buildBaseGeosets();

    auto eraseGroup = [&](uint16_t group) {
        for (auto it = geosets.begin(); it != geosets.end();) {
            if ((*it / 100) == group) {
                it = geosets.erase(it);
            } else {
                ++it;
            }
        }
    };

    // CharGeosets: group 4=gloves(forearm), 5=boots(shin), 8=sleeves, 13=pants
    std::unordered_set<uint16_t> modelGeosets;
    if (const auto* modelData = charRenderer_->getModelData(PREVIEW_MODEL_ID)) {
        for (const auto& batch : modelData->batches) {
            modelGeosets.insert(batch.submeshId);
        }
    }

    // core/geoset_rules.hpp, the same rule the world paths use.
    //
    // This copy took a second id as its fallback rather than looking inside the
    // group, so where the two ids given were the same - which every call below
    // does - it could only answer "the model has it" or "nothing", and a model
    // spelling that part with a different variant lost it entirely.
    auto pickGeoset = [&](uint16_t preferred, uint16_t fallback) -> uint16_t {
        const uint16_t chosen = core::resolveGeoset(preferred, modelGeosets);
        if (chosen != 0) return chosen;
        return (fallback != 0 && modelGeosets.count(fallback) > 0) ? fallback : 0;
    };

    auto lowestInGroup = [&](uint16_t group) -> uint16_t {
        return core::resolveGeoset(static_cast<uint16_t>(group * 100 + 2), modelGeosets);
    };

    uint16_t geosetGloves = pickGeoset(core::kGeosetBareForearms, core::kGeosetBareForearms);
    uint16_t geosetBoots = pickGeoset(core::kGeosetBareShins, lowestInGroup(5));
    uint16_t geosetSleeves = pickGeoset(core::kGeosetBareSleeves, core::kGeosetBareSleeves);
    uint16_t geosetPants = pickGeoset(core::kGeosetBarePants, core::kGeosetBarePants);

    // Chest/Shirt/Robe → group 8 (sleeves)
    {
        uint32_t did = findDisplayId({4, 5, 20});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        if (gg > 0) geosetSleeves = pickGeoset(core::equippedGeoset(core::equipment::kChestBare, gg), core::kGeosetBareSleeves);
        // Robe kilt legs
        uint32_t gg3 = getGeosetGroup(did, geosetGroup3Field);
        if (gg3 > 0) geosetPants = pickGeoset(core::equippedGeoset(core::equipment::kRobeKiltBare, gg3), core::kGeosetBarePants);
    }
    // Legs → group 13 (trousers)
    {
        uint32_t did = findDisplayId({7});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        if (gg > 0) geosetPants = pickGeoset(core::equippedGeoset(core::equipment::kLegsBare, gg), core::kGeosetBarePants);
    }
    // Boots → group 5 (shins)
    {
        uint32_t did = findDisplayId({8});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        if (gg > 0) geosetBoots = pickGeoset(core::equippedGeoset(core::equipment::kBootsBare, gg), lowestInGroup(5));
    }
    // Gloves → group 4 (forearms)
    {
        uint32_t did = findDisplayId({10});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        if (gg > 0) geosetGloves = pickGeoset(core::equippedGeoset(core::equipment::kGlovesBare, gg), core::kGeosetBareForearms);
    }
    // Wrists/Bracers → group 8 (sleeves, only if chest/shirt didn't set it)
    {
        uint32_t did = findDisplayId({9});
        if (did != 0 && geosetSleeves == pickGeoset(core::kGeosetBareSleeves, core::kGeosetBareSleeves)) {
            uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
            if (gg > 0) geosetSleeves = pickGeoset(core::equippedGeoset(core::equipment::kChestBare, gg), core::kGeosetBareSleeves);
        }
    }
    // Belt → group 18 (buckle), falling back to the base variant so the waist
    // is not left empty - see the spawner's copy for what that costs.
    uint16_t geosetBelt = 0;
    {
        uint32_t did = findDisplayId({6});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        geosetBelt = pickGeoset(
            gg > 0 ? core::equippedGeoset(core::equipment::kBeltBase, gg) : 0,
            core::equipment::kBeltBase);
    }

    eraseGroup(4);
    eraseGroup(5);
    eraseGroup(8);
    eraseGroup(13);
    eraseGroup(15);
    eraseGroup(18);
    if (geosetGloves != 0) geosets.insert(geosetGloves);
    if (geosetBoots != 0) geosets.insert(geosetBoots);
    if (geosetSleeves != 0) geosets.insert(geosetSleeves);
    if (geosetPants != 0) geosets.insert(geosetPants);
    if (geosetBelt != 0) geosets.insert(geosetBelt);
    uint16_t geosetCape = pickGeoset(
        hasInvType({16}) ? core::kGeosetWithCape : core::kGeosetNoCape,
        core::kGeosetNoCape);
    if (geosetCape != 0) geosets.insert(geosetCape); // Cloak mesh toggle (visual may still be limited)
    if (hasInvType({19})) {
        uint16_t geosetTabard = pickGeoset(core::kGeosetDefaultTabard, 0);
        if (geosetTabard != 0) geosets.insert(geosetTabard);
    }

    // The masks apply only after a real head attachment succeeds. Missing art
    // leaves the selected hair and facial features visible; a circlet's zero
    // masks preserve them too. Rebuilding the base set above restores them on
    // removal or replacement without carrying the previous helm's state.
    if (attachedHelmDisplayId != 0) {
        const uint8_t modelSex = (gender_ == game::Gender::FEMALE ||
            (gender_ == game::Gender::NONBINARY && useFemaleModel_)) ? 1u : 0u;
        auto visibilityDbc = assetManager_->loadDBC("HelmetGeosetVisData.dbc");
        const auto visibility = core::resolveHelmVisibility(displayInfoDbc.get(),
            visibilityDbc.get(), idiL, attachedHelmDisplayId,
            static_cast<uint8_t>(race_), modelSex);
        core::applyHelmVisibility(visibility, geosets, modelGeosets);
    }

    charRenderer_->setActiveGeosets(instanceId_, geosets);

    // --- Textures (equipment overlays onto body skin) ---
    if (bodySkinPath_.empty()) return true; // geosets applied, but can't composite


    // Texture component region fields - use DBC layout when available, fall back to binary offsets.
    uint32_t texRegionFields[8];
    pipeline::getItemDisplayInfoTextureFields(*displayInfoDbc, idiL, texRegionFields);

    std::vector<std::pair<int, std::string>> regionLayers;
    regionLayers.reserve(32);

    for (const auto& it : equipment) {
        if (it.displayModel == 0) continue;
        int32_t recIdx = displayInfoDbc->findRecordById(it.displayModel);
        if (recIdx < 0) continue;

        for (int region = 0; region < 8; region++) {
            std::string texName = displayInfoDbc->getString(static_cast<uint32_t>(recIdx), texRegionFields[region]);
            if (texName.empty()) continue;

            const std::string fullPath = pipeline::resolveItemRegionTexture(
                *assetManager_, region, texName, gender_ == game::Gender::FEMALE);
            if (fullPath.empty()) continue;
            regionLayers.emplace_back(region, fullPath);
        }
    }

    if (!regionLayers.empty()) {
        VkTexture* newTex = charRenderer_->compositeWithRegions(bodySkinPath_, baseLayers_, regionLayers);
        if (newTex != nullptr) {
            charRenderer_->setTextureSlotOverride(instanceId_, static_cast<uint16_t>(skinTextureSlotIndex_), newTex);
        }
    }

    // Cloak texture (group 15) is separate from body compositing.
    if (hasInvType({16})) {
        uint32_t capeDisplayId = findDisplayId({16});
        if (capeDisplayId != 0) {
            int32_t capeRecIdx = displayInfoDbc->findRecordById(capeDisplayId);
            if (capeRecIdx >= 0) {
                std::vector<std::string> capeNames;
                auto addName = [&](const std::string& n) {
                    if (!n.empty() && std::find(capeNames.begin(), capeNames.end(), n) == capeNames.end()) {
                        capeNames.push_back(n);
                    }
                };
                std::string leftName = displayInfoDbc->getString(static_cast<uint32_t>(capeRecIdx), 3);
                std::string rightName = displayInfoDbc->getString(static_cast<uint32_t>(capeRecIdx), 4);
                if (gender_ == game::Gender::FEMALE) {
                    addName(rightName);
                    addName(leftName);
                } else {
                    addName(leftName);
                    addName(rightName);
                }

                // pipeline/item_textures.hpp knows where a cape's art is and
                // in what order to try for it.
                std::vector<std::string> candidates;
                for (const auto& nameRaw : capeNames) {
                    for (auto& c : pipeline::capeTextureCandidates(
                             nameRaw, gender_ == game::Gender::FEMALE)) {
                        if (std::find(candidates.begin(), candidates.end(), c) == candidates.end()) {
                            candidates.push_back(std::move(c));
                        }
                    }
                }
                VkTexture* whiteTex = charRenderer_->loadTexture("");
                for (const auto& c : candidates) {
                    VkTexture* capeTex = charRenderer_->loadTexture(c);
                    if (capeTex != nullptr && capeTex != whiteTex) {
                        charRenderer_->setGroupTextureOverride(instanceId_, 15, capeTex);
                        if (const auto* md = charRenderer_->getModelData(PREVIEW_MODEL_ID)) {
                            for (size_t ti = 0; ti < md->textures.size(); ti++) {
                                if (md->textures[ti].type == 2) {
                                    charRenderer_->setTextureSlotOverride(instanceId_, static_cast<uint16_t>(ti), capeTex);
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    } else {
        if (const auto* md = charRenderer_->getModelData(PREVIEW_MODEL_ID)) {
            for (size_t ti = 0; ti < md->textures.size(); ti++) {
                if (md->textures[ti].type == 2) {
                    charRenderer_->clearTextureSlotOverride(instanceId_, static_cast<uint16_t>(ti));
                }
            }
        }
    }

    return true;
}

/// Any model, by path, with none of the appearance work.
///
/// loadCharacter builds a player: a race model, a composited skin, geosets
/// chosen from hair and facial hair, underwear. A creature is none of that -
/// its M2 names its own textures and has no geoset choices to make - so this
/// is the same three steps with the middle two thirds left out: read the M2,
/// frame the camera on its bounds, hand it to the renderer and stand it up.
///
/// The renderer is the one that already draws every unit in the world, so a
/// creature model needs nothing here that the world does not already do.
bool CharacterPreview::setBakedSkin(const std::string& bakePath) {
    if (!charRenderer_ || instanceId_ == 0 || bakePath.empty()) return false;
    VkTexture* tex = charRenderer_->loadTexture(bakePath);
    if (!tex) return false;
    charRenderer_->setTextureSlotOverride(
        instanceId_, static_cast<uint16_t>(skinTextureSlotIndex_), tex);
    return true;
}

bool CharacterPreview::loadCreature(
        const std::string& m2Path,
        const std::vector<std::pair<uint32_t, std::string>>& skins) {
    if (!charRenderer_ || !assetManager_ || !assetManager_->isInitialized()) {
        return false;
    }
    if (m2Path.empty()) return false;
    releaseCharacterModel();

    auto modelAsset = std::make_shared<pipeline::M2Model>();
    auto& model = *modelAsset;
    if (!loadPreviewM2(m2Path, model)) {
        LOG_WARNING("CharacterPreview: could not read creature model: ", m2Path);
        return false;
    }

    if (camera_) {
        // The declared bounds where there are any, and the vertices where
        // there are not - the same choice loadCharacter makes, and for the
        // same reason: a model whose header bounds are wrong frames wrong.
        glm::vec3 frameMin = model.boundMin;
        glm::vec3 frameMax = model.boundMax;
        if (!model.vertices.empty()) {
            glm::vec3 tightMin(std::numeric_limits<float>::max());
            glm::vec3 tightMax(-std::numeric_limits<float>::max());
            for (const auto& v : model.vertices) {
                if (!isFiniteVec3(v.position)) continue;
                tightMin = glm::min(tightMin, v.position);
                tightMax = glm::max(tightMax, v.position);
            }
            if (tightMin.x <= tightMax.x && tightMin.y <= tightMax.y &&
                tightMin.z <= tightMax.z) {
                frameMin = tightMin;
                frameMax = tightMax;
            }
        }
        frameCameraForModelBounds(*camera_, frameMin, frameMax);
        modelBoundMinZ_ = frameMin.z;
        modelBoundMaxZ_ = frameMax.z;
        readPortraitFraming(model);
        modelBoundMin_=frameMin; modelBoundMax_=frameMax; previewFitScale_=-1;
        fullBodyDistance_ = camera_->getPosition().y;
    }

    if (!charRenderer_->loadSharedModel(modelAsset, PREVIEW_MODEL_ID)) {
        LOG_WARNING("CharacterPreview: failed to upload creature model");
        return false;
    }

    instanceId_ = charRenderer_->createInstance(PREVIEW_MODEL_ID,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glue::previewModelRotation(modelYaw_),
        1.0f);
    if (instanceId_ == 0) {
        LOG_WARNING("CharacterPreview: failed to create creature instance");
        return false;
    }

    // The display's skins, into the slots the model declared for them. Without
    // this the geometry draws with no texture at all, which is what the target
    // frame's portrait showed: a white shape in the right silhouette.
    if (const auto* data = charRenderer_->getModelData(PREVIEW_MODEL_ID)) {
        for (size_t ti = 0; ti < data->textures.size(); ++ti) {
            for (const auto& [texType, path] : skins) {
                if (data->textures[ti].type != texType) continue;
                if (VkTexture* tex = charRenderer_->loadTexture(path)) {
                    charRenderer_->setModelTexture(PREVIEW_MODEL_ID,
                                                   static_cast<uint32_t>(ti), tex);
                }
                break;
            }
        }
    }

    // No geoset selection: a creature shows every submesh its skin declares,
    // which is what leaving the active set alone means.
    charRenderer_->playAnimation(instanceId_, rendering::anim::STAND, true);
    modelLoaded_ = true;
    return true;
}

bool CharacterPreview::loadPreviewM2(const std::string& m2Path, pipeline::M2Model& outModel) {
    if (!assetManager_) return false;
    return pipeline::loadM2WithSkin(*assetManager_, m2Path, outModel);  // m2_asset_loader.hpp
}

uint32_t CharacterPreview::attachHelm(const std::vector<game::EquipmentItem>& equipment) {
    if (!charRenderer_ || !assetManager_ || instanceId_ == 0) return 0;

    // Standard SMSG_CHAR_ENUM order: head is slot zero. Attachment zero is
    // the shield mount; only attachment 11 is removed and replaced here.
    charRenderer_->detachWeapon(instanceId_, core::kAttachHelm);
    // NPC/remote-player equipment is a compact list, unlike CHAR_ENUM's
    // indexed array. Slot zero may therefore be a weapon, not a helmet.
    const game::EquipmentItem* head=nullptr;
    if(equipment.size()>=19){
        if(equipment[0].inventoryType==1 && equipment[0].displayModel)head=&equipment[0];
    }else for(const auto& item:equipment)if(item.inventoryType==1 && item.displayModel){head=&item;break;}
    if(!head)return 0;
    const uint32_t displayId = head->displayModel;
    const uint8_t modelSex = (gender_ == game::Gender::FEMALE ||
        (gender_ == game::Gender::NONBINARY && useFemaleModel_)) ? 1u : 0u;
    const auto helm = core::resolveHelmVisual(*assetManager_, displayId,
        static_cast<uint8_t>(race_), modelSex);
    if (!helm.valid()) return 0;

    pipeline::M2Model model;
    std::string path = helm.racialModelPath;
    if (path.empty() || !loadPreviewM2(path, model) || !model.isValid()) {
        model = {};
        path = helm.baseModelPath;
        if (!loadPreviewM2(path, model) || !model.isValid()) {
            LOG_WARNING("CharacterPreview: helm model unavailable for display ", displayId,
                        " racial=", helm.racialModelPath, " base=", helm.baseModelPath);
            return 0;
        }
    }

    // The renderer owns the child instance and updates its matrix from the
    // animated parent bone every frame. Parent removal recursively destroys
    // the attachment and retires its GPU model; no preview-side copy survives.
    if (!charRenderer_->attachWeapon(instanceId_, core::kAttachHelm, model,
                                    PREVIEW_HELM_MODEL_ID, helm.texturePath)) {
        // attachWeapon can upload successfully and fail to create an instance.
        charRenderer_->unloadModelIfUnused(PREVIEW_HELM_MODEL_ID);
        LOG_WARNING("CharacterPreview: could not attach helm display ", displayId);
        return 0;
    }
    LOG_INFO("CharacterPreview: attached helm display=", displayId, " model=", path,
             " texture=", helm.texturePath, " attachment=", core::kAttachHelm);
    return displayId;
}

void CharacterPreview::attachWeapons(const std::vector<game::EquipmentItem>& equipment) {
    if (!charRenderer_ || !assetManager_ || instanceId_ == 0) return;

    // Attachment 0 = shield, 1 = right hand, 2 = left hand.
    charRenderer_->detachWeapon(instanceId_, 0);
    charRenderer_->detachWeapon(instanceId_, 1);
    charRenderer_->detachWeapon(instanceId_, 2);

    auto displayInfoDbc = assetManager_->loadDBC("ItemDisplayInfo.dbc");
    if (!displayInfoDbc || !displayInfoDbc->isLoaded()) return;


    for (bool offHand : {false, true}) {
        const auto* item = previewEquipmentForHand(equipment, offHand);
        if (!item) continue;
        const uint32_t displayId = item->displayModel;
        // SMSG_CHAR_ENUM already reports the enchant as its ItemVisual id.
        const uint32_t itemVisualId = item->enchantment;
        const uint32_t attachmentId = offHand ? (item->inventoryType == game::InvType::SHIELD ? 0 : 2) : 1;

        int32_t recIdx = displayInfoDbc->findRecordById(displayId);
        if (recIdx < 0) continue;

        const auto art = pipeline::readItemDisplayArt(*displayInfoDbc,
                                                      static_cast<uint32_t>(recIdx));
        if (art.modelFile.empty()) continue;
        const std::string& modelFile = art.modelFile;
        const std::string& textureName = art.textureName;

        pipeline::M2Model weaponModel;
        std::string m2Path = "Item\\ObjectComponents\\Weapon\\" + modelFile;
        if (!loadPreviewM2(m2Path, weaponModel)) {
            m2Path = "Item\\ObjectComponents\\Shield\\" + modelFile;
            if (!loadPreviewM2(m2Path, weaponModel)) {
                LOG_WARNING("CharacterPreview: failed to load weapon model ", modelFile);
                continue;
            }
        }

        std::string texturePath;
        if (!textureName.empty()) {
            texturePath = "Item\\ObjectComponents\\Weapon\\" + textureName + ".blp";
            if (!assetManager_->fileExists(texturePath)) {
                texturePath = "Item\\ObjectComponents\\Shield\\" + textureName + ".blp";
            }
        }

        const uint32_t weaponModelId = previewModelIdFor(m2Path);
        if (!charRenderer_->attachWeapon(instanceId_, attachmentId, weaponModel,
                                         weaponModelId, texturePath)) {
            continue;
        }
        attachWeaponEnchantVisual(attachmentId, itemVisualId);
    }
}

uint32_t CharacterPreview::previewModelIdFor(const std::string& assetKey) {
    auto it = previewModelIds_.find(assetKey);
    if (it != previewModelIds_.end()) return it->second;
    uint32_t id = nextPreviewModelId_++;
    previewModelIds_.emplace(assetKey, id);
    return id;
}

void CharacterPreview::attachWeaponEnchantVisual(uint32_t attachmentId, uint32_t itemVisualId) {
    if (itemVisualId == 0 || !charRenderer_ || !assetManager_) return;

    auto visualsDbc = assetManager_->loadDBC("ItemVisuals.dbc");
    auto effectsDbc = assetManager_->loadDBC("ItemVisualEffects.dbc");
    if (!visualsDbc || !visualsDbc->isLoaded() || !effectsDbc || !effectsDbc->isLoaded()) return;

    auto effectModels = pipeline::resolveItemVisualModels(itemVisualId, visualsDbc.get(),
                                                          effectsDbc.get());

    for (uint32_t visualSlot = 0; visualSlot < effectModels.size(); ++visualSlot) {
        const std::string& modelName = effectModels[visualSlot];
        if (modelName.empty()) continue;

        std::string m2Path = modelName;
        size_t dot = m2Path.rfind('.');
        m2Path = (dot != std::string::npos ? m2Path.substr(0, dot) : m2Path) + ".m2";

        pipeline::M2Model effectModel;
        if (!loadPreviewM2(m2Path, effectModel)) {
            LOG_WARNING("CharacterPreview: failed to load enchant visual ", m2Path);
            continue;
        }
        charRenderer_->attachWeaponEffect(instanceId_, attachmentId, visualSlot,
                                          effectModel, previewModelIdFor(m2Path));
    }
}

void CharacterPreview::loadRacialBackdrop(game::Race race) {
    // Nothing to stand in front of when the background is meant to show
    // through. Skipped here rather than removed afterwards, so a portrait does
    // not read and build a scene on every rebuild only to discard it.
    if (transparentBackground_) return;

    if (!charRenderer_ || !assetManager_) return;
    if (backdropRace_ == static_cast<int>(race) && backdropInstanceId_ != 0) {
        // Appearance changes recreate the character instance but keep the racial
        // scene. Reapply its stand mark and camera rig to the new instance.
        applyPreviewView();
        return;
    }

    if (backdropInstanceId_ != 0) {
        charRenderer_->removeInstance(backdropInstanceId_);
        backdropInstanceId_ = 0;
    }
    if (sceneEffects_) sceneEffects_->clear();
    hasSceneCamera_ = false;
    backdropRace_ = static_cast<int>(race);
    previewStandPosition_ = glm::vec3(0.0f);
    previewViewDirection_ = glm::vec3(0.0f, 1.0f, 0.0f);
    modelYaw_ = 90.0f;
    applyPreviewView();

    // The glue screens each stand the character in their racial home - humans in
    // Stormwind, orcs in Durotar, and so on. Undead reuse the Scourge scene.
    const char* sceneName = nullptr;
    switch (race) {
        case game::Race::HUMAN:     sceneName = "UI_Human";    break;
        case game::Race::ORC:       sceneName = "UI_Orc";      break;
        case game::Race::DWARF:     sceneName = "UI_Dwarf";    break;
        case game::Race::NIGHT_ELF: sceneName = "UI_NightElf"; break;
        case game::Race::UNDEAD:    sceneName = "UI_Scourge";  break;
        case game::Race::TAUREN:    sceneName = "UI_Tauren";   break;
        // Blizzard does not ship separate Gnome or Troll glue scenes in the
        // Classic/TBC/WotLK asset sets. These races intentionally share their
        // faction partner's authored selection backdrop.
        case game::Race::GNOME:     sceneName = "UI_Dwarf";    break;
        case game::Race::TROLL:     sceneName = "UI_Orc";      break;
        case game::Race::BLOOD_ELF: sceneName = "UI_BloodElf"; break;
        case game::Race::DRAENEI:   sceneName = "UI_Draenei";  break;
        default: break;
    }
    if (!sceneName) return;

    std::string scenePath = std::string("Interface\\Glues\\Models\\") + sceneName + "\\" +
                            sceneName + ".m2";
    pipeline::M2Model sceneModel;
    if (!loadPreviewM2(scenePath, sceneModel)) {
        failPreview("Racial 3D scene could not be read: " + scenePath);
        return;
    }

    // These scenes are authored in their own space - the human one sits ~230 units
    // from its origin - and carry the camera and the spot the character stands on.
    // Without both there is no way to place the scene, so leave it out entirely
    // rather than drop geometry somewhere off-screen.
    if (sceneModel.cameras.empty()) {
        failPreview("Racial 3D scene has no camera: " + scenePath);
        return;
    }
    if (!takeSceneCamera(sceneModel)) return;

    // Attachment 0 is the character's mark on the scene's ground.
    glm::vec3 standPos = sceneCameraTargetBase_;
    for (const auto& att : sceneModel.attachments) {
        if (att.id == 0) { standPos = att.position; break; }
    }

    if (!charRenderer_->loadModel(sceneModel, PREVIEW_BACKDROP_MODEL_ID)) {
        failPreview("Racial 3D scene upload failed: " + scenePath);
        return;
    }

    backdropInstanceId_ = charRenderer_->createInstance(PREVIEW_BACKDROP_MODEL_ID, glm::vec3(0.0f));
    if (backdropInstanceId_ == 0) { failPreview("Racial 3D scene instance could not be created."); return; }
    charRenderer_->setInstanceSceneModel(backdropInstanceId_, true);
    charRenderer_->playAnimation(backdropInstanceId_, sceneModel.sequences.empty() ? 0u : sceneModel.sequences[0].id, true);
    loadSceneEffects(sceneModel);
#ifdef WOWEE_PS4
    sceneDiagKey_ = scenePath;
    firstCompositeDiagnosed_ = false;
    loadSceneDiagState();
#endif

    // Keep the character on the scene's authored stand mark and use the scene
    // camera only for viewing direction. Distance and focus come from our portrait
    // rig so scroll-wheel zoom can move naturally toward the face.
    glm::vec3 toCamera = sceneCameraPosition_ - sceneCameraTarget_;
    if (glm::length(toCamera) < 0.001f) toCamera = glm::vec3(0.0f, 1.0f, 0.0f);
    previewStandPosition_ = standPos;
    previewViewDirection_ = glm::normalize(toCamera);
    modelYaw_ = glm::degrees(std::atan2(previewViewDirection_.y, previewViewDirection_.x));
    applyPreviewView();

    LOG_INFO("CharacterPreview: racial backdrop ", scenePath,
             " stand=(", standPos.x, ",", standPos.y, ",", standPos.z, ")");
}

void CharacterPreview::update(float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) deltaTime = 0.0f;
    if (charRenderer_ && modelLoaded_) {
        // Racial glue scenes place the character far from world origin (the
        // human scene is roughly 230 units away). Use this preview's camera for
        // animation distance checks; testing against the renderer's default
        // origin culled bone and weapon-attachment updates, leaving the paper
        // doll rigid and its equipped weapons invisible.
        const glm::vec3 cameraPos = camera_ ? camera_->getPosition()
                                            : previewStandPosition_;
        charRenderer_->update(deltaTime, cameraPos);
        animateSceneCamera(deltaTime);
        if (sceneEffects_ && sceneEffects_->loaded()) {
            CharacterRenderer::SceneFrame frame;
            const uint32_t sceneId = backdropInstanceId_ ? backdropInstanceId_ : instanceId_;
            if (charRenderer_->getSceneFrame(sceneId, frame)) sceneEffects_->update(deltaTime, frame);
        }
    }
}

void CharacterPreview::render() {
    // No-op - actual rendering happens in the renderer graph's preview_composite pass
}

void CharacterPreview::compositePass(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Only composite when a UI screen actually requested it this frame
    if (!compositeRequested_) return;
    compositeRequested_ = false;

    if (!charRenderer_ || !camera_ || !modelLoaded_ || !renderTarget_ || !renderTarget_->isValid()) {
        return;
    }

    uint32_t fi = frameIndex % MAX_FRAMES;

    // Update per-frame UBO with preview camera matrices and studio lighting
    GPUPerFrameData ubo{};
    ubo.view = camera_->getViewMatrix();
    ubo.projection = camera_->getProjectionMatrix();
    ubo.lightSpaceMatrix = glm::mat4(1.0f);
    if (!previewUBOMapped_[fi] || !previewPerFrameSet_[fi]) {
        failPreview("3D scene camera buffer is unavailable.");
        return;
    }
    // Studio lighting: key light from upper-right-front
    ubo.lightDir = glm::vec4(glm::normalize(glm::vec3(0.5f, -0.7f, 0.5f)), 0.0f);
    ubo.lightColor = glm::vec4(1.0f, 0.95f, 0.9f, 0.0f);
    ubo.ambientColor = glm::vec4(0.35f, 0.35f, 0.4f, 0.0f);
    ubo.viewPos = glm::vec4(camera_->getPosition(), 0.0f);
    // No fog in preview
    ubo.fogColor = glm::vec4(0.05f, 0.05f, 0.1f, 0.0f);
    ubo.fogParams = glm::vec4(9999.0f, 10000.0f, 0.0f, 0.0f);
    // Off-screen preview has no real shadow pass/light-space setup. Sampling
    // the global shadow binding here can produce unstable fragments on some
    // drivers, so keep the portrait on studio lighting only.
    ubo.shadowParams = glm::vec4(0.0f);

    std::memcpy(previewUBOMapped_[fi], &ubo, sizeof(GPUPerFrameData));

    // Begin off-screen render pass
    // Nothing at all behind a portrait, so the frame art around it shows
    // through; the studio backdrop everywhere else.
    VkClearColorValue clearColor = transparentBackground_
        ? VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}}
        : VkClearColorValue{{0.05f, 0.05f, 0.1f, 1.0f}};
#ifdef WOWEE_PS4
    // 0: clear only (the menus work without the scene); 1: draw as on the
    // desktop (default); 2: the first composite goes out one draw per submit.
    static const int sceneLevel = [] {
        const char* raw = std::getenv("WOWEE_PS4_SCENE_LEVEL");
        if (!raw || !*raw) return 1;
        if (raw[0] == '0') return 0;
        if (raw[0] == '1') return 1;
        return 2;
    }();
    if (sceneLevel == 0) {
        failPreview("3D scene disabled by WOWEE_PS4_SCENE_LEVEL=0.");
        return;
    }
    if (sceneLevel == 2 && !firstCompositeDiagnosed_) {
        firstCompositeDiagnosed_ = true;
        diagnoseFirstComposite(fi, clearColor);
    }
#else
    constexpr int sceneLevel = 1;
#endif

    // Preview rendering bypasses Renderer::renderWorld(), so it must run the
    // same resource-preparation hook itself after server appearance data has
    // produced bone matrices. This preserves lazy data loading while keeping
    // GPU allocation outside CharacterRenderer::render().
    charRenderer_->prepareRender(fi);

    renderTarget_->beginPass(cmd, clearColor);
    // Render the character model
    if (sceneLevel != 0) charRenderer_->render(cmd, previewPerFrameSet_[fi], *camera_);
    if (sceneEffects_ && sceneEffects_->loaded())
        sceneEffects_->render(cmd, previewPerFrameSet_[fi], *camera_, fi);

    renderTarget_->endPass(cmd);

    if (charRenderer_->lastRenderDrawCount() == 0 &&
        (!sceneEffects_ || !sceneEffects_->loaded())) {
        failPreview("The 3D scene produced no drawable mesh or effects.");
        compositeRendered_ = false;
        return;
    }

    if (!compositeRendered_) {
        LOG_INFO("CharacterPreview: first complete composite scene=", sceneDiagKey_,
                 " draws=", charRenderer_->lastRenderDrawCount(), " target=", fboWidth_, "x", fboHeight_);
#ifdef WOWEE_PS4
        platform::ps4::reportBootStage("scene: first complete 3D composite recorded");
#endif
    }
    compositeRendered_ = true;
}

void CharacterPreview::diagnoseFirstComposite(uint32_t frameSlot, const VkClearColorValue& clearColor) {
#ifdef WOWEE_PS4
    if (!vkCtx_ || !charRenderer_ || !renderTarget_ || !camera_) return;
    if (sceneDiagLoadedKey_ != sceneDiagKey_) loadSceneDiagState();
    char line[640];
    std::snprintf(line, sizeof(line), "scene diag: begin %s (one draw per submit; forced=%u; %zu draw(s) skipped from earlier launches)",
                  sceneDiagKey_.c_str(), sceneDiag_.forced, sceneDiag_.bad.size());
    platform::ps4::reportBootStage(line);
    LOG_INFO(line);
    uint32_t total = 0;
    for (uint32_t k = 0; k < 256; ++k) {
        VkCommandBuffer c = vkCtx_->beginSingleTimeCommands();
        if (c == VK_NULL_HANDLE) {
            platform::ps4::reportBootStage("scene diag: no immediate command buffer; skipped");
            return;
        }
        renderTarget_->beginPass(c, clearColor);
        charRenderer_->prepareRender(frameSlot);
        charRenderer_->render(c, previewPerFrameSet_[frameSlot], *camera_, static_cast<int>(k));
        renderTarget_->endPass(c);
        total = charRenderer_->lastRenderDrawCount();
        if (k >= total) {
            // Nothing but the pass itself in this one; it submits harmlessly.
            vkCtx_->endSingleTimeCommands(c);
            break;
        }
        const std::string desc = charRenderer_->lastFilteredDrawDescription();
        if (desc.empty()) {
            // On the skip list: the pass went out without it.
            std::snprintf(line, sizeof(line), "scene diag: submit %u/%u skipped (faulted in an earlier launch)", k + 1u, total);
            platform::ps4::reportBootStage(line);
            vkCtx_->endSingleTimeCommands(c);
            continue;
        }
        std::snprintf(line, sizeof(line), "scene diag: submit %u/%u: %s", k + 1u, total, desc.c_str());
        platform::ps4::reportBootStage(line);
        LOG_INFO(line);
        // Written before the submit: if the process dies in it, the next
        // launch reads which draw it was and goes on from there.
        sceneDiag_.attempting = static_cast<int>(k);
        sceneDiag_.attemptingForced = sceneDiag_.forced;
        sceneDiag_.attemptingDesc = desc;
        saveSceneDiagState();
        vkCtx_->endSingleTimeCommands(c);
        sceneDiag_.attempting = -1;
        sceneDiag_.attemptingDesc.clear();
        saveSceneDiagState();
        std::snprintf(line, sizeof(line), "scene diag: submit %u/%u completed", k + 1u, total);
        platform::ps4::reportBootStage(line);
    }
    std::snprintf(line, sizeof(line), "scene diag: all %u draws completed one by one (forced=%u, %zu skipped)",
                  total, sceneDiag_.forced, sceneDiag_.bad.size());
    platform::ps4::reportBootStage(line);
    LOG_INFO(line);
#else
    (void)frameSlot; (void)clearColor;
#endif
}

#ifdef WOWEE_PS4
namespace {

std::string sceneDiagFilePath() {
    return platform::ps4::writableRoot() + "/config/scene_diag.txt";
}

// One block per scene:
//   scene=<key>
//   bad=1,7
//   forced=3
//   attempting=-1
//   attempting_forced=0
//   attempting_desc=...
//   note=...
using SceneDiagFile = std::map<std::string, CharacterPreview::SceneDiagState>;

SceneDiagFile readSceneDiagFile() {
    SceneDiagFile file;
    std::ifstream in(sceneDiagFilePath());
    if (!in) return file;
    std::string key;
    std::string lineText;
    while (std::getline(in, lineText)) {
        if (!lineText.empty() && lineText.back() == '\r') lineText.pop_back();
        const size_t eq = lineText.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = lineText.substr(0, eq);
        const std::string value = lineText.substr(eq + 1);
        if (name == "scene") { key = value; file[key]; continue; }
        if (key.empty()) continue;
        CharacterPreview::SceneDiagState& st = file[key];
        if (name == "bad") {
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (!item.empty()) st.bad.push_back(static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10)));
            }
        } else if (name == "forced") {
            st.forced = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (name == "attempting") {
            st.attempting = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
        } else if (name == "attempting_forced") {
            st.attemptingForced = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (name == "attempting_desc") {
            st.attemptingDesc = value;
        } else if (name == "note") {
            st.notes.push_back(value);
        }
    }
    return file;
}

void writeSceneDiagFile(const SceneDiagFile& file) {
    const std::string path = sceneDiagFilePath();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    for (const auto& [key, st] : file) {
        out << "scene=" << key << '\n';
        out << "bad=";
        for (size_t i = 0; i < st.bad.size(); ++i) out << (i ? "," : "") << st.bad[i];
        out << '\n';
        out << "forced=" << st.forced << '\n';
        out << "attempting=" << st.attempting << '\n';
        out << "attempting_forced=" << st.attemptingForced << '\n';
        out << "attempting_desc=" << st.attemptingDesc << '\n';
        for (const auto& n : st.notes) out << "note=" << n << '\n';
        out << '\n';
    }
    out.flush();
}

const char* forcedName(unsigned forced) {
    switch (forced & 7u) {
        case 0: return "nothing";
        case 1: return "opaque pipeline";
        case 2: return "white texture";
        case 3: return "opaque pipeline + white texture";
        case 4: return "simple material";
        case 5: return "opaque pipeline + simple material";
        case 6: return "white texture + simple material";
        default: return "opaque pipeline + white texture + simple material";
    }
}

} // namespace

void CharacterPreview::loadSceneDiagState() {
    SceneDiagFile file = readSceneDiagFile();
    const SceneDiagState previous = file[sceneDiagKey_];
    sceneDiag_ = {};
    sceneDiagLoadedKey_ = sceneDiagKey_;
    char line[768];
    if (previous.attempting >= 0 || previous.forced || !previous.bad.empty()) {
        std::snprintf(line, sizeof(line),
            "scene diag: prior B4 state draw=%d forced=%u skipped=%zu; B5 restores every original draw",
            previous.attempting, previous.forced, previous.bad.size());
        LOG_WARNING(line);
        platform::ps4::reportBootStage(line);
        sceneDiag_.notes.emplace_back(line);
        file[sceneDiagKey_] = sceneDiag_;
        writeSceneDiagFile(file);
    }
    // Explicit diagnostic overrides only; previous faults never silently
    // turn original materials white or permanently remove scene geometry.
    if (const char* skip = std::getenv("WOWEE_PS4_SCENE_SKIP_DRAWS"); skip && *skip) {
        std::stringstream ss(skip);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) sceneDiag_.bad.push_back(static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10)));
        }
    }
    if (const char* force = std::getenv("WOWEE_PS4_SCENE_FORCE"); force && *force) {
        sceneDiag_.forced |= static_cast<unsigned>(std::strtoul(force, nullptr, 10)) & 7u;
    }
    std::sort(sceneDiag_.bad.begin(), sceneDiag_.bad.end());
    sceneDiag_.bad.erase(std::unique(sceneDiag_.bad.begin(), sceneDiag_.bad.end()), sceneDiag_.bad.end());
    std::string badList;
    for (size_t i = 0; i < sceneDiag_.bad.size(); ++i) badList += (i ? "," : "") + std::to_string(sceneDiag_.bad[i]);
    std::snprintf(line, sizeof(line), "scene diag: state for %s: forced=%s skipped=[%s]",
                  sceneDiagKey_.c_str(), forcedName(sceneDiag_.forced), badList.c_str());
    platform::ps4::reportBootStage(line);
    LOG_INFO(line);
    applySceneDiagOverrides();
}

void CharacterPreview::saveSceneDiagState() const {
    SceneDiagFile file = readSceneDiagFile();
    file[sceneDiagKey_] = sceneDiag_;
    writeSceneDiagFile(file);
}

void CharacterPreview::applySceneDiagOverrides() {
    if (!charRenderer_) return;
    CharacterRenderer::DrawOverrides o;
    o.skipDraws = sceneDiag_.bad;
    o.forceOpaque = (sceneDiag_.forced & 1u) != 0;
    o.whiteTexture = (sceneDiag_.forced & 2u) != 0;
    o.simpleMaterial = (sceneDiag_.forced & 4u) != 0;
    charRenderer_->setDrawOverrides(std::move(o));
}
#else
void CharacterPreview::loadSceneDiagState() {}
void CharacterPreview::saveSceneDiagState() const {}
void CharacterPreview::applySceneDiagOverrides() {}
#endif

void CharacterPreview::rotate(float yawDelta) {
    modelYaw_ += yawDelta;
    if (instanceId_ > 0 && charRenderer_) {
        charRenderer_->setInstanceRotation(instanceId_, glue::previewModelRotation(modelYaw_));
    }
}

void CharacterPreview::zoom(float wheelDelta) {
    if (!std::isfinite(wheelDelta) || wheelDelta == 0.0f) return;
    zoomLevel_ = std::clamp(zoomLevel_ + wheelDelta * 0.12f, 0.0f, 1.0f);
    applyPreviewView();
}

void CharacterPreview::setTransparentBackground(bool transparent) {
    transparentBackground_ = transparent;
    // The scene model behind the character is as opaque as the clear colour,
    // so it goes as well.
    if (transparent && backdropInstanceId_ != 0 && charRenderer_) {
        charRenderer_->removeInstance(backdropInstanceId_);
        backdropInstanceId_ = 0;
        backdropRace_ = -1;
        if (sceneEffects_) sceneEffects_->clear();
    }
}

void CharacterPreview::readPortraitFraming(const pipeline::M2Model& model) {
    hasAuthoredPortrait_ = false;
    ui::PortraitModel metrics;
    metrics.boundMinZ = modelBoundMinZ_;
    metrics.boundMaxZ = modelBoundMaxZ_;
    for (const auto& att : model.attachments) if (att.id == 11 && isFiniteVec3(att.position)) {
        metrics.headZ = att.position.z;
        metrics.headForward = att.position.x;
        break;
    }
    for (const auto& cam : model.cameras) if (cam.type == 0 &&
        isFiniteVec3(cam.targetBase) && isFiniteVec3(cam.positionBase)) {
        portraitCameraPosition_ = cam.positionBase + glue::samplePosition(cam.positions,0,0,0,model.globalSequenceDurations);
        portraitCameraTarget_ = cam.targetBase + glue::samplePosition(cam.targets,0,0,0,model.globalSequenceDurations);
        const float cameraDistance=glm::length(portraitCameraPosition_-portraitCameraTarget_);
        if (!isFiniteVec3(portraitCameraPosition_) || !isFiniteVec3(portraitCameraTarget_) ||
            !std::isfinite(cameraDistance) || cameraDistance<=.05f ||
            !std::isfinite(cam.fov) || cam.fov<=.05f || cam.fov>=3.f) continue;
        metrics.hasPortraitCamera = true;
        metrics.cameraTargetZ = portraitCameraTarget_.z;
        metrics.cameraDistance = cameraDistance;
        metrics.cameraFovRadians = cam.fov;
        hasAuthoredPortrait_ = true;
        break;
    }
    const auto framing = ui::portraitFraming(metrics);
    portraitFocusZ_ = framing.focusZ;
    portraitDistance_ = framing.distance;
    portraitFov_ = framing.fovDegrees;
    LOG_INFO("[PORTRAIT_FRAME] source=", int(framing.source), " focusZ=", portraitFocusZ_,
             " distance=", portraitDistance_, " headZ=", metrics.headZ,
             " authored=", hasAuthoredPortrait_, " camera=", portraitCameraPosition_.x, ",",
             portraitCameraPosition_.y, ",", portraitCameraPosition_.z,
             " target=", portraitCameraTarget_.x, ",", portraitCameraTarget_.y, ",", portraitCameraTarget_.z,
             " fov=", portraitFov_);
}

void CharacterPreview::setPortraitFraming() {
    portraitView_ = true;
    sceneCameraMode_=false;
    previewStandPosition_=glm::vec3(0.f);
    if(camera_)camera_->setFov(portraitFov_);
    zoomLevel_ = 1.0f;
    // Straight at the character. The model is turned to face the camera in the
    // same breath, which is the part loadRacialBackdrop would otherwise be the
    // only place to do.
    previewViewDirection_ = glm::vec3(0.0f, 1.0f, 0.0f);
    modelYaw_ = glm::degrees(std::atan2(previewViewDirection_.y,
                                        previewViewDirection_.x));
    applyPreviewView();
}

void CharacterPreview::resetView() {
    portraitView_ = false;
    if (camera_) camera_->setFov(30.f);
    zoomLevel_ = 0.0f;
    applyPreviewView();
}

bool CharacterPreview::takeSceneCamera(const pipeline::M2Model& scene) {
    if (scene.cameras.empty()) return failPreview("The 3D scene has no authored camera.");
    const auto authored = std::find_if(scene.cameras.begin(), scene.cameras.end(),
        [](const pipeline::M2Camera& camera) { return camera.type == 0xffffffffu; });
    const auto& cam = authored == scene.cameras.end() ? scene.cameras.front() : *authored;
    if (!glue::finite(cam.positionBase) || !glue::finite(cam.targetBase))
        return failPreview("The 3D scene camera contains invalid coordinates.");
    sceneCameraPositionBase_ = cam.positionBase;
    sceneCameraTargetBase_ = cam.targetBase;
    sceneCameraFov_ = cam.fov;
    sceneCameraNear_ = std::isfinite(cam.nearClip) && cam.nearClip > 0 ? cam.nearClip : 0.5f;
    sceneCameraFar_ = std::isfinite(cam.farClip) && cam.farClip > sceneCameraNear_ ? cam.farClip : 30000.0f;
    sceneCameraPositions_ = cam.positions;
    sceneCameraTargets_ = cam.targets;
    sceneCameraRolls_ = cam.roll;
    sceneGlobalDurations_ = scene.globalSequenceDurations;
    sceneCameraLoopMs_ = scene.sequences.empty() ? 0u : scene.sequences[0].duration;
    sceneCameraClockMs_ = 0.0f;
    sceneCameraPosition_ = sceneCameraPositionBase_ + glue::samplePosition(cam.positions, 0, 0, 0, sceneGlobalDurations_);
    sceneCameraTarget_ = sceneCameraTargetBase_ + glue::samplePosition(cam.targets, 0, 0, 0, sceneGlobalDurations_);
    sceneCameraRoll_ = glue::sampleRoll(cam.roll, 0, 0, 0, sceneGlobalDurations_);
    previewFitEye_=sceneCameraPosition_; previewFitTarget_=sceneCameraTarget_; previewFitScale_=-1;
    hasSceneCamera_ = true;
    LOG_INFO("CharacterPreview: scene camera type=", cam.type,
             " base=(", cam.positionBase.x, ",", cam.positionBase.y, ",", cam.positionBase.z,
             ") target=(", cam.targetBase.x, ",", cam.targetBase.y, ",", cam.targetBase.z,
             ") diagonalFov=", cam.fov, " near=", sceneCameraNear_, " far=", sceneCameraFar_,
             " positionInterpolation=", cam.positions.interpolationType,
             " targetInterpolation=", cam.targets.interpolationType,
             " rollInterpolation=", cam.roll.interpolationType);
    return true;
}

void CharacterPreview::animateSceneCamera(float deltaTime) {
    if (!sceneCameraMode_ || !hasSceneCamera_ || !charRenderer_) return;
    CharacterRenderer::SceneFrame frame;
    const uint32_t sceneId = backdropInstanceId_ ? backdropInstanceId_ : instanceId_;
    if (!charRenderer_->getSceneFrame(sceneId, frame)) return;
    (void)deltaTime;
    // Use the skeleton's sequence, local time and global time. A separate
    // camera clock drifts whenever the animation wraps or changes sequence.
    sceneCameraClockMs_ = frame.timeMs;
    sceneCameraPosition_ = sceneCameraPositionBase_ + glue::samplePosition(
        sceneCameraPositions_, frame.sequence, frame.timeMs, frame.globalTimeMs, sceneGlobalDurations_);
    sceneCameraTarget_ = sceneCameraTargetBase_ + glue::samplePosition(
        sceneCameraTargets_, frame.sequence, frame.timeMs, frame.globalTimeMs, sceneGlobalDurations_);
    sceneCameraRoll_ = glue::sampleRoll(sceneCameraRolls_, frame.sequence,
        frame.timeMs, frame.globalTimeMs, sceneGlobalDurations_);
    applySceneCamera();
}

void CharacterPreview::applySceneCamera() {
    if (!camera_ || !hasSceneCamera_) return;
    const glm::vec3 camPos = sceneCameraPosition_;
    glm::vec3 forward = sceneCameraTarget_ - camPos;
    if (glm::dot(forward, forward) < 1e-6f) forward = glm::vec3(0.0f, -1.0f, 0.0f);
    forward = glm::normalize(forward);
    camera_->setPosition(camPos);
    camera_->setRotation(glm::degrees(std::atan2(forward.y, forward.x)),
                         glm::degrees(std::asin(std::clamp(forward.z, -1.0f, 1.0f))));
    camera_->setRoll(sceneCameraRoll_);
    camera_->setClipPlanes(sceneCameraNear_, sceneCameraFar_);
    const float aspect = static_cast<float>(fboWidth_) / static_cast<float>(fboHeight_);
    camera_->setFov(glm::degrees(glue::scaledVerticalFov(sceneCameraFov_, aspect, sceneScale_)));
    if (instanceId_ > 0 && charRenderer_ && backdropInstanceId_ != 0) {
        const float fit=preview::fitScale(previewFitEye_,previewFitTarget_,previewStandPosition_,
            modelBoundMin_,modelBoundMax_,glue::scaledVerticalFov(sceneCameraFov_,aspect,sceneScale_),aspect);
        if (std::abs(fit-previewFitScale_)>.001f) {
            LOG_INFO("[PREVIEW_FIT] avatarScale=",fit," modelHeight=",modelBoundMaxZ_-modelBoundMinZ_,
                     " sceneScale=",sceneScale_," backdropScale=1");
            previewFitScale_=fit;
        }
        charRenderer_->setInstanceScale(instanceId_,fit);
        charRenderer_->setInstancePosition(instanceId_, previewStandPosition_);
        charRenderer_->setInstanceRotation(instanceId_, glue::previewModelRotation(modelYaw_));
    }
}

void CharacterPreview::setSceneCameraMode(bool enabled) {
    sceneCameraMode_ = enabled;
    applyPreviewView();
}

void CharacterPreview::setSceneScale(float scale) {
    sceneScale_ = std::isfinite(scale) ? std::clamp(scale, 1.0f, 2.0f) : 1.0f;
    applyPreviewView();
}

bool CharacterPreview::loadGlueScene(const std::string& m2Path) {
    if (!charRenderer_ || !assetManager_ || !assetManager_->isInitialized() || m2Path.empty()) {
        return failPreview("3D scene assets or renderer are not initialized.");
    }
    if (instanceId_ > 0) {
        charRenderer_->removeInstance(instanceId_);
        instanceId_ = 0;
    }
    if (backdropInstanceId_ != 0) {
        charRenderer_->removeInstance(backdropInstanceId_);
        backdropInstanceId_ = 0;
        backdropRace_ = -1;
    }
    modelLoaded_ = false;
    compositeRendered_ = false;
    hasSceneCamera_ = false;
    lastError_.clear();

    pipeline::M2Model scene;
    if (!loadPreviewM2(m2Path, scene)) {
        return failPreview("3D scene could not be read: " + m2Path);
    }
    if (scene.cameras.empty()) {
        return failPreview("3D scene has no authored camera: " + m2Path);
    }
    const uint32_t modelId = previewModelIdFor(m2Path);
    if (!charRenderer_->loadModel(scene, modelId)) {
        return failPreview("3D scene upload failed: " + m2Path);
    }
    instanceId_ = charRenderer_->createInstance(modelId, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f);
    if (instanceId_ == 0) {
        return failPreview("3D scene instance could not be created: " + m2Path);
    }
    charRenderer_->setInstanceSceneModel(instanceId_, true);
    charRenderer_->playAnimation(instanceId_, scene.sequences.empty() ? 0u : scene.sequences[0].id, true);

    if (!takeSceneCamera(scene)) return false;
    loadSceneEffects(scene);
    sceneCameraMode_ = true;
    previewStandPosition_ = glm::vec3(0.0f);
    modelLoaded_ = true;
    applySceneCamera();
    LOG_INFO("CharacterPreview: glue scene ", m2Path, " camera=(", sceneCameraPosition_.x, ",",
             sceneCameraPosition_.y, ",", sceneCameraPosition_.z, ") fov=", sceneCameraFov_,
             " animations=", scene.sequences.size());
#ifdef WOWEE_PS4
    // A new scene is a new set of draws: diagnose its first composite too,
    // with whatever an earlier launch learned about it.
    sceneDiagKey_ = m2Path;
    firstCompositeDiagnosed_ = false;
    loadSceneDiagState();
#endif
    return true;
}

void CharacterPreview::applyPreviewView() {
    if (!camera_) return;
    if (sceneCameraMode_ && hasSceneCamera_) {
        applySceneCamera();
        return;
    }

    camera_->setRoll(0.0f);
    camera_->setClipPlanes(0.1f, 30000.0f);
    const float modelHeight = std::max(modelBoundMaxZ_ - modelBoundMinZ_, 0.1f);
    const float bodyFocusZ = (modelBoundMinZ_ + modelBoundMaxZ_) * 0.5f;
    const float faceFocusZ = portraitView_ ? portraitFocusZ_ : modelBoundMinZ_ + modelHeight * 0.82f;
    const float focusZ = bodyFocusZ + (faceFocusZ - bodyFocusZ) * zoomLevel_;

    // Stay outside the near plane while getting close enough to inspect facial
    // textures and hair. Large races retain proportionally more distance.
    const float faceDistance = portraitView_ ? portraitDistance_ : std::max(1.15f, modelHeight * 0.70f);
    const float distance = fullBodyDistance_ +
                           (std::min(faceDistance, fullBodyDistance_) - fullBodyDistance_) * zoomLevel_;
    glm::vec3 focus = previewStandPosition_ + glm::vec3(0.0f, 0.0f, focusZ);
    glm::vec3 camPos = focus + previewViewDirection_ * distance;
    if (portraitView_ && hasAuthoredPortrait_) {
        // Preserve both endpoints, including forward lean and camera pitch.
        // Reducing the authored camera to distance + target Z moved it toward
        // Orc faces and away from Human faces by different amounts.
        const auto pose = placePortraitCamera(portraitCameraPosition_, portraitCameraTarget_, previewStandPosition_, modelYaw_);
        focus = pose.target;
        camPos = pose.eye;
        camera_->setFov(portraitFov_);
    }
    camera_->setPosition(camPos);

    const glm::vec3 forward = glm::normalize(focus - camPos);
    const float yaw = glm::degrees(std::atan2(forward.y, forward.x));
    const float pitch = glm::degrees(std::asin(std::clamp(forward.z, -1.0f, 1.0f)));
    camera_->setRotation(yaw, pitch);

    if (instanceId_ > 0 && charRenderer_) {
        charRenderer_->setInstanceScale(instanceId_,1.0f);
        charRenderer_->setInstancePosition(instanceId_, previewStandPosition_);
        charRenderer_->setInstanceRotation(instanceId_, glue::previewModelRotation(modelYaw_));
    }
}

} // namespace rendering
} // namespace wowee
