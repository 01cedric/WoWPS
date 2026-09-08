#include "ui/wotlk_glue.hpp"
#include "ui/wotlk_button_style.hpp"
#include "pipeline/asset_manager.hpp"
#include "rendering/vk_context.hpp"
#include "core/logger.hpp"
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string>

namespace wowee::ui {
namespace {
constexpr const char* kLogo = "Interface/Glues/Common/Glues-WoW-WotLKLogo.blp";
constexpr const char* kButtonUp = "Interface/Buttons/UI-Panel-Button-Up.blp";
constexpr const char* kButtonDown = "Interface/Buttons/UI-Panel-Button-Down.blp";
constexpr const char* kButtonDisabled = "Interface/Buttons/UI-Panel-Button-Disabled.blp";
constexpr const char* kButtonHighlight = "Interface/Buttons/UI-Panel-Button-Highlight.blp";
constexpr const char* kEditLeft = "Interface/Common/Common-Input-BorderLeft.blp";
constexpr const char* kEditMiddle = "Interface/Common/Common-Input-BorderMid.blp";
constexpr const char* kEditRight = "Interface/Common/Common-Input-BorderRight.blp";
std::string keyFor(const char* path) {
    std::string key = path ? path : "";
    for (auto& c : key) { if (c == '\\') c = '/'; else if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; }
    return key;
}
uint32_t memoryType(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p{};
    vkGetPhysicalDeviceMemoryProperties(physical, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}
}
struct WotlkGlue::Impl {
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };
    rendering::VkContext* context = nullptr;
    bool attempted = false;
    std::map<std::string, Texture> textures;
    void release(Texture& t) {
        const auto device = context->getDevice();
        if (t.descriptor && ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
            ImGui_ImplVulkan_RemoveTexture(t.descriptor);
        if (t.sampler) vkDestroySampler(device, t.sampler, nullptr);
        if (t.view) vkDestroyImageView(device, t.view, nullptr);
        if (t.image) vkDestroyImage(device, t.image, nullptr);
        if (t.memory) vkFreeMemory(device, t.memory, nullptr);
        t = {};
    }
    bool upload(const pipeline::BLPImage& pixels, Texture& texture) {
        const auto device = context->getDevice();
        const auto physical = context->getPhysicalDevice();
        const auto size = VkDeviceSize(pixels.width) * pixels.height * 4;
        if (!pixels.isValid() || pixels.width > 4096 || pixels.height > 4096 ||
            pixels.isBlockCompressed() || size == 0 || pixels.data.size() < size) return false;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        const auto cleanup = [&]() {
            if (staging) vkDestroyBuffer(device, staging, nullptr);
            if (stagingMemory) vkFreeMemory(device, stagingMemory, nullptr);
        };
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = size; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (vkCreateBuffer(device, &buffer, nullptr, &staging) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memoryType(physical, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (allocate.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(device, &allocate, nullptr, &stagingMemory) != VK_SUCCESS ||
            vkBindBufferMemory(device, staging, stagingMemory, 0) != VK_SUCCESS) { cleanup(); return false; }
        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMemory, 0, size, 0, &mapped) != VK_SUCCESS || !mapped) { cleanup(); return false; }
        std::memcpy(mapped, pixels.data.data(), static_cast<size_t>(size));
        vkUnmapMemory(device, stagingMemory);
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {static_cast<uint32_t>(pixels.width), static_cast<uint32_t>(pixels.height), 1}; image.mipLevels = 1; image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (vkCreateImage(device, &image, nullptr, &texture.image) != VK_SUCCESS) { cleanup(); return false; }
        vkGetImageMemoryRequirements(device, texture.image, &requirements);
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (allocate.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(device, &allocate, nullptr, &texture.memory) != VK_SUCCESS ||
            vkBindImageMemory(device, texture.image, texture.memory, 0) != VK_SUCCESS) { cleanup(); return false; }
        // Every handle above is checked before recording. One complete RGBA8
        // subresource is compatible with the PS4 optimal-image upload path.
        VkCommandBuffer command = context->beginSingleTimeCommands();
        if (!command) { cleanup(); return false; }
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = image.extent;
        vkCmdCopyBufferToImage(command, staging, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (!context->endSingleTimeCommands(command)) { cleanup(); return false; }
        cleanup();
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = texture.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &view, nullptr, &texture.view) != VK_SUCCESS) return false;
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &sampler, nullptr, &texture.sampler) != VK_SUCCESS) return false;
        texture.descriptor = ImGui_ImplVulkan_AddTexture(texture.sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return texture.descriptor != VK_NULL_HANDLE;
    }
};
WotlkGlue::WotlkGlue() : impl_(std::make_unique<Impl>()) {}
WotlkGlue::~WotlkGlue() {
    if (!impl_->context || !impl_->context->getDevice()) return;
    vkDeviceWaitIdle(impl_->context->getDevice());
    for (auto& entry : impl_->textures) impl_->release(entry.second);
}
void WotlkGlue::ensureLoaded(pipeline::AssetManager* assets, rendering::VkContext* context) {
    if (impl_->attempted || !assets || !assets->isInitialized() || !context || !context->getDevice()) return;
    impl_->attempted = true; impl_->context = context;
    static const char* paths[] = {
        kLogo, "Interface/Glues/Common/Glues-WoW-Logo.blp", kButtonUp, kButtonDown, kButtonDisabled, kButtonHighlight,
        kEditLeft, kEditMiddle, kEditRight,
        "Interface/Glues/CharacterCreate/UI-CharacterCreate-Races.blp",
        "Interface/Glues/CharacterCreate/UI-CharacterCreate-Classes.blp",
        "Interface/Glues/CharacterSelect/Glues-CharacterSelect-Select.blp"
    };
    for (const auto* path : paths) {
        if (!assets->fileExists(path)) { LOG_WARNING("Glue skin: client asset missing: ", path); continue; }
        auto image = assets->loadTexture(path, false);
        if (path == kButtonUp || path == kButtonDown) {
            const auto changed = glue_button::blueEnamel(image.data);
            LOG_INFO("Glue skin: blue button enamel texels=", changed, " path=", path,
                     " artwork_uv=0,0..0.625,0.6875");
        }
        Impl::Texture texture;
        if (!impl_->upload(image, texture)) { impl_->release(texture); LOG_WARNING("Glue skin: texture upload rejected: ", path); continue; }
        impl_->textures.emplace(keyFor(path), texture);
        LOG_INFO("Glue skin: loaded ", path, " ", image.width, "x", image.height);
    }
}
bool WotlkGlue::drawAsset(ImDrawList* list, const char* path, ImVec2 a, ImVec2 b,
                         ImVec2 uv0, ImVec2 uv1, ImU32 tint) const {
    const auto found = impl_->textures.find(keyFor(path));
    if (!list || found == impl_->textures.end()) return false;
    list->AddImage(reinterpret_cast<ImTextureID>(found->second.descriptor), a, b, uv0, uv1, tint);
    return true;
}
bool WotlkGlue::drawLogo(ImDrawList* list, ImVec2 a, ImVec2 b) const {
    return drawAsset(list, kLogo, a, b) || drawAsset(list, "Interface/Glues/Common/Glues-WoW-Logo.blp", a, b);
}
void WotlkGlue::drawButton(ImDrawList* list, ImVec2 a, ImVec2 b, bool hovered, bool held, bool enabled) const {
    if (!list || b.x <= a.x || b.y <= a.y) return;
    const char* path = !enabled ? kButtonDisabled : held ? kButtonDown : kButtonUp;
    const auto it = impl_->textures.find(keyFor(path));
    if (it != impl_->textures.end()) {
        glue_button::drawStrip(list, reinterpret_cast<ImTextureID>(it->second.descriptor), a, b);
    } else {
        list->AddRectFilled(a, b, !enabled ? IM_COL32(45, 45, 48, 245) : held ? IM_COL32(10, 34, 73, 255) : IM_COL32(14, 65, 115, 255), 2);
        list->AddRect(a, b, IM_COL32(152, 119, 72, 255), 2, 0, 2);
    }
    if (hovered && enabled) {
        const auto highlight = impl_->textures.find(keyFor(kButtonHighlight));
        if (highlight != impl_->textures.end())
            glue_button::drawStrip(list, reinterpret_cast<ImTextureID>(highlight->second.descriptor),
                                   a, b, IM_COL32(255, 255, 255, 150));
        else
            list->AddRect(a, b, IM_COL32(255, 209, 90, 255), 2, 0, 2);
    }
}
void WotlkGlue::drawField(ImDrawList* list, ImVec2 a, ImVec2 b, bool focused) const {
    list->AddRectFilled(a, b, IM_COL32(0, 0, 0, 215), 2);
    const float cap = std::min((b.y - a.y) * .25f, (b.x - a.x) * .2f);
    const bool hasEdges = impl_->textures.count(keyFor(kEditLeft)) && impl_->textures.count(keyFor(kEditMiddle)) && impl_->textures.count(keyFor(kEditRight));
    if (hasEdges) {
        drawAsset(list, kEditLeft, a, {a.x + cap, b.y});
        drawAsset(list, kEditMiddle, {a.x + cap, a.y}, {b.x - cap, b.y});
        drawAsset(list, kEditRight, {b.x - cap, a.y}, b);
    } else list->AddRect(a, b, IM_COL32(146, 139, 123, 255), 2, 0, 1);
    if (focused) list->AddRect(a, b, IM_COL32(220, 172, 64, 210), 2, 0, 1);
}
void WotlkGlue::drawFrame(ImDrawList* list, ImVec2 a, ImVec2 b) const {
    list->AddRectFilled(a, b, IM_COL32(8, 8, 10, 226), 3);
    list->AddRect(a, b, IM_COL32(103, 95, 80, 255), 3, 0, 3);
    list->AddRect({a.x + 3, a.y + 3}, {b.x - 3, b.y - 3}, IM_COL32(47, 42, 33, 255), 2, 0, 1);
}
}
