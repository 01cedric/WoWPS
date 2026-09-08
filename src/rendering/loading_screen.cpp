#include "rendering/loading_screen.hpp"
#include "rendering/loading_screen_layout.hpp"
#include "rendering/vk_context.hpp"
#include "core/logger.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/client_loading_screens.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#ifdef WOWEE_PS4
#include "platform/ps4/imgui_impl_ps4.h"
#else
#include <imgui_impl_sdl2.h>
#endif
#include <SDL2/SDL.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

// This translation unit also supplies stb_image to the desktop image loaders.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace wowee::rendering {
namespace {
uint32_t findMemoryType(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    return UINT32_MAX;
}
bool inGuiFrame() {
    const ImGuiContext* ctx = ImGui::GetCurrentContext();
    return ctx && ctx->WithinFrameScope;
}
ImVec2 minimum(const LoadingRect& rect) { return {rect.x, rect.y}; }
ImVec2 maximum(const LoadingRect& rect) { return {rect.x + rect.width, rect.y + rect.height}; }
}

LoadingScreen::~LoadingScreen() noexcept { shutdown(); }

bool LoadingScreen::initialize() {
    ready = vkCtx && vkCtx->getDevice() && ImGui::GetCurrentContext();
    LOG_INFO("Loading screen initialized: ready=", ready, " original_painting=",
             background.descriptor != VK_NULL_HANDLE, " original_bar=",
             border.descriptor != VK_NULL_HANDLE && fill.descriptor != VK_NULL_HANDLE);
    return ready;
}

void LoadingScreen::resetProgress() { loadProgress = 0; statusText.clear(); }
void LoadingScreen::setProgress(float progress) {
    loadProgress = advanceLoadingProgress(loadProgress, progress);
}
void LoadingScreen::setStatus(const std::string& status) {
    if (statusText == status) return;
    statusText = status;
    // Detailed steps belong in logs, not on top of the original loading art.
    LOG_INFO("World loading: ", statusText, " progress=", loadProgress);
}

void LoadingScreen::destroyTexture(VkDevice device, const Texture& texture) {
    if (texture.view) vkDestroyImageView(device, texture.view, nullptr);
    if (texture.image) vkDestroyImage(device, texture.image, nullptr);
    if (texture.memory) vkFreeMemory(device, texture.memory, nullptr);
}

void LoadingScreen::shutdown() noexcept {
    ready = false;
    if (!vkCtx || !vkCtx->getDevice()) return;
    const VkDevice device = vkCtx->getDevice();
    const VkDescriptorPool pool = std::exchange(descriptorPool, VK_NULL_HANDLE);
    const VkDescriptorSetLayout layout = std::exchange(descriptorLayout, VK_NULL_HANDLE);
    auto textures = std::move(retiredTextures);
    // Destruction can run while unwinding std::bad_alloc. Keep the three
    // current handles on the stack; appending them to the retired vector
    // would allocate even on a second, already-empty shutdown.
    const std::array<Texture, 3> current{{std::exchange(background, Texture{}),
        std::exchange(border, Texture{}), std::exchange(fill, Texture{})}};
    sampler = VK_NULL_HANDLE;
    backgroundWide = false;
    travelRoute = {};
    routeArtworkAvailable = false;
    const bool hasCurrent = std::any_of(current.begin(), current.end(), [](const Texture& texture) {
        return texture.image || texture.view || texture.memory;
    });
    if (!pool && !layout && !hasCurrent && textures.empty()) return;
    auto destroy = [device, pool, layout, current, textures = std::move(textures)]() noexcept {
        // Sets are owned here, not by ImGui. A backend restart therefore cannot
        // invalidate a painting while a blocking world load is using it.
        if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
        for (const auto& texture : current) destroyTexture(device, texture);
        for (const auto& texture : textures) destroyTexture(device, texture);
        if (layout) vkDestroyDescriptorSetLayout(device, layout, nullptr);
    };
    if (vkCtx->isInUploadBatch() || inGuiFrame()) {
        // Includes resources referred to by the current, not-yet-submitted UI
        // frame. Captures every handle by value; never captures this object.
        try {
            vkCtx->deferAfterAllFrameFences(std::move(destroy));
        } catch (...) {
            // The std::function, shared counter and frame queues can allocate,
            // including a failure after only some fence slots were queued.
            // Do not destroy here: wait-idle cannot fence an unsubmitted frame.
            // Under this exceptional OOM the raw GPU objects remain allocated
            // until device teardown. This deliberately prefers bounded leakage
            // to a GPU use-after-free or a second exception during unwinding.
            std::fputs("LoadingScreen: deferred cleanup allocation failed; GPU resources retained until device teardown\n", stderr);
        }
    } else {
        vkCtx->waitAllUploads();
        vkDeviceWaitIdle(device);
        destroy();
    }
}

bool LoadingScreen::createDescriptorResources() {
    if (descriptorPool) return sampler != VK_NULL_HANDLE;
    if (!vkCtx || !vkCtx->getDevice()) return false;
    VkDevice device = vkCtx->getDevice();
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout) != VK_SUCCESS)
        return false;
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 16;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        descriptorLayout = VK_NULL_HANDLE;
        return false;
    }
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler = vkCtx->getOrCreateSampler(samplerInfo);
    return sampler != VK_NULL_HANDLE;
}

bool LoadingScreen::uploadTexture(const unsigned char* pixels, uint32_t width,
                                  uint32_t height, Texture& out) {
    if (!pixels || !width || !height || width > 4096 || height > 4096 ||
        !createDescriptorResources()) return false;
    const VkDevice device = vkCtx->getDevice();
    const VkPhysicalDevice physical = vkCtx->getPhysicalDevice();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    Texture texture;
    auto cleanupStaging = [&] {
        if (staging) vkDestroyBuffer(device, staging, nullptr);
        if (stagingMemory) vkFreeMemory(device, stagingMemory, nullptr);
        staging = VK_NULL_HANDLE; stagingMemory = VK_NULL_HANDLE;
    };
    auto fail = [&](const char* stage) {
        LOG_ERROR("Loading texture upload failed at ", stage, " (", width, "x", height, ")");
        cleanupStaging();
        if (texture.descriptor) vkFreeDescriptorSets(device, descriptorPool, 1, &texture.descriptor);
        destroyTexture(device, texture);
        return false;
    };
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &staging) != VK_SUCCESS) return fail("buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, staging, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocation.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &allocation, nullptr, &stagingMemory) != VK_SUCCESS)
        return fail("staging memory");
    if (vkBindBufferMemory(device, staging, stagingMemory, 0) != VK_SUCCESS) return fail("bind buffer");
    void* mapped = nullptr;
    if (vkMapMemory(device, stagingMemory, 0, bytes, 0, &mapped) != VK_SUCCESS || !mapped)
        return fail("map staging");
    std::memcpy(mapped, pixels, static_cast<size_t>(bytes));
    vkUnmapMemory(device, stagingMemory);

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &texture.image) != VK_SUCCESS) return fail("image");
    vkGetImageMemoryRequirements(device, texture.image, &requirements);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocation.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &allocation, nullptr, &texture.memory) != VK_SUCCESS) return fail("image memory");
    if (vkBindImageMemory(device, texture.image, texture.memory, 0) != VK_SUCCESS) return fail("bind image");
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &texture.view) != VK_SUCCESS) return fail("view");
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = descriptorPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &descriptorLayout;
    if (vkAllocateDescriptorSets(device, &setInfo, &texture.descriptor) != VK_SUCCESS) return fail("descriptor");
    VkDescriptorImageInfo imageDescriptor{sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture.descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    bool recorded = false;
    const bool submitted = vkCtx->immediateSubmit([&](VkCommandBuffer cmd) {
        if (!cmd) return;
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        cmdPipelineBarrier2(cmd, dependency);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cmdPipelineBarrier2(cmd, dependency);
        recorded = true;
    });
    if (!recorded || !submitted) return fail("record/submit copy");
    if (vkCtx->isInUploadBatch()) {
        vkCtx->deferRawStagingCleanup(staging, stagingMemory);
        staging = VK_NULL_HANDLE; stagingMemory = VK_NULL_HANDLE;
    } else cleanupStaging();
    if (out.image) retiredTextures.push_back(out);
    out = texture;
    return true;
}

bool LoadingScreen::loadAssetTexture(pipeline::AssetManager& assets, const std::string& path, Texture& out) {
    if (!assets.fileExists(path)) return false;
    auto image = assets.loadTexture(path, false);
    const uint64_t expected = static_cast<uint64_t>(image.width) * image.height * 4;
    if (!image.isValid() || image.isBlockCompressed() || !expected || expected > image.data.size()) return false;
    const bool ok = uploadTexture(image.data.data(), image.width, image.height, out);
    if (ok) LOG_INFO("Loading UI asset: ", path, " ", image.width, "x", image.height);
    return ok;
}

bool LoadingScreen::setBackgroundFromAssets(pipeline::AssetManager* assets, uint32_t mapId) {
    shutdown();
    resetProgress();
    if (!assets || !assets->isInitialized() || !vkCtx) return false;
    const auto extent = vkCtx->getSwapchainExtent();
    const bool wideOutput = extent.height && static_cast<float>(extent.width) / extent.height > 1.4f;
    const auto maps = assets->loadDBCOptional("Map.dbc");
    const auto screens = assets->loadDBCOptional("LoadingScreens.dbc");
    const auto selected = pipeline::resolveClientLoadingScreen(maps.get(), screens.get(), mapId, wideOutput);
    bool loaded = false;
    for (const auto& candidate : selected.candidates) {
        if (!loadAssetTexture(*assets, candidate.path, background)) continue;
        backgroundWide = candidate.wide;
        routeArtworkAvailable = selected.matchedMap && selected.matchedScreen &&
            candidate.path.find("LoadScreenGeneric") == std::string::npos;
        loaded = true;
        LOG_INFO("World loading artwork map=", mapId, " screen=", selected.screenId,
                 " map_match=", selected.matchedMap, " screen_match=", selected.matchedScreen,
                 " wide=", backgroundWide, " source=", candidate.path);
        break;
    }
    const bool borderOk = loadAssetTexture(*assets, pipeline::ClientLoadingBarBorder, border);
    const bool fillOk = loadAssetTexture(*assets, pipeline::ClientLoadingBarFill, fill);
    if (!loaded) LOG_WARNING("No original loading painting available for map ", mapId);
    if (!borderOk || !fillOk) LOG_WARNING("Original loading bar assets incomplete: border=", borderOk, " fill=", fillOk);
    return loaded;
}

bool LoadingScreen::setTransportRouteFromAssets(pipeline::AssetManager* assets, uint32_t pathId, uint32_t legIndex) {
    travelRoute = {};
    if (!assets || !routeArtworkAvailable) return false;
    const auto splines = assets->loadDBCOptional("LoadingScreenTaxiSplines.dbc");
    travelRoute = pipeline::resolveClientTravelSpline(splines.get(), pathId, legIndex);
    LOG_INFO("[TRAVEL_LOADING] path=", pathId, " leg=", legIndex, " row=", travelRoute.rowId,
             " points=", travelRoute.count, " authoredRoute=", travelRoute.valid());
    return travelRoute.valid();
}

void LoadingScreen::renderOverlay() {
    if (!ready || !inGuiFrame()) return;
    const auto& io = ImGui::GetIO();
    const auto layout = layoutLoadingScreen(io.DisplaySize.x, io.DisplaySize.y, backgroundWide, loadProgress);
    if (layout.painting.width <= 0) return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(0, 0, 0, 255));
    if (background.descriptor)
        draw->AddImage(reinterpret_cast<ImTextureID>(background.descriptor), minimum(layout.painting), maximum(layout.painting));
    // Transport route strokes/cross are intentionally not overlaid here.
    // Stock map loading artwork must remain unmodified.
    const auto full = layoutLoadingScreen(io.DisplaySize.x, io.DisplaySize.y, backgroundWide, 1.0f).fill;
    draw->AddRectFilled(minimum(full), maximum(full), IM_COL32(5, 8, 14, 255));
    if (layout.fill.width > 0) {
        if (fill.descriptor) {
            // Crop the filled part in screen AND texture space. Stretching the
            // complete texture to a short bar squashes the original highlights.
            draw->AddImage(reinterpret_cast<ImTextureID>(fill.descriptor), minimum(layout.fill), maximum(layout.fill),
                           {0, 0}, {layout.fillU, 1});
        } else draw->AddRectFilled(minimum(layout.fill), maximum(layout.fill), IM_COL32(40, 86, 166, 255));
    }
    if (border.descriptor)
        draw->AddImage(reinterpret_cast<ImTextureID>(border.descriptor), minimum(layout.border), maximum(layout.border));
    else draw->AddRect(minimum(full), maximum(full), IM_COL32(155, 155, 155, 255));
    if (!background.descriptor) {
        const char* text = "Loading artwork is unavailable in the client data";
        const auto size = ImGui::CalcTextSize(text);
        draw->AddText({(io.DisplaySize.x - size.x) * 0.5f, io.DisplaySize.y * 0.5f}, IM_COL32_WHITE, text);
    }
}

void LoadingScreen::render() {
    if (!ready || !vkCtx) return;
    if (inGuiFrame()) {
        renderOverlay();
        return; // Never end or replace an ImGui frame owned by the caller.
    }
    // The owner must submit its upload batch before a standalone loading
    // frame. Sampling an image whose copy is merely recorded is undefined.
    if (vkCtx->isInUploadBatch()) {
        LOG_WARNING("Loading frame deferred until the enclosing upload batch is submitted");
        return;
    }
    if (vkCtx->isSwapchainDirty() && sdlWindow) {
        int width = 0, height = 0;
        SDL_GetWindowSize(sdlWindow, &width, &height);
        if (width > 0 && height > 0 && !vkCtx->recreateSwapchain(width, height)) return;
    }
    uint32_t imageIndex = 0;
    VkCommandBuffer cmd = vkCtx->beginFrame(imageIndex);
    if (!cmd) return;
    const auto& framebuffers = vkCtx->getOverlayFramebuffers();
    const VkRenderPass pass = vkCtx->getOverlayClearRenderPass();
    if (!pass || imageIndex >= framebuffers.size() || !framebuffers[imageIndex]) {
        LOG_ERROR("Loading frame has no valid overlay target, image=", imageIndex);
        vkCtx->endFrame(cmd, imageIndex);
        return;
    }
    ImGui_ImplVulkan_NewFrame();
#ifdef WOWEE_PS4
    ImGui_ImplPS4_NewFrame(0, 0);
#else
    ImGui_ImplSDL2_NewFrame();
#endif
    auto& io = ImGui::GetIO();
    const bool hadCursor = io.MouseDrawCursor;
    io.MouseDrawCursor = false;
    ImGui::NewFrame();
    renderOverlay();
    ImGui::Render();
    io.MouseDrawCursor = hadCursor;
    VkClearValue clear{};
    clear.color = {{0, 0, 0, 1}};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = pass;
    begin.framebuffer = framebuffers[imageIndex];
    begin.renderArea.extent = vkCtx->getSwapchainExtent();
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
    vkCtx->endFrame(cmd, imageIndex);
}

} // namespace wowee::rendering
