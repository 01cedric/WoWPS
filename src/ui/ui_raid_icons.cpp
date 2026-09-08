#include "ui/ui_raid_icons.hpp"
#include "ui/ui_texture_load.hpp"

#include "core/application.hpp"
#include "pipeline/asset_manager.hpp"
#include "rendering/vk_context.hpp"

#include <array>
#include <string>

namespace wowee {
namespace ui {

VkDescriptorSet getRaidTargetIcon(uint8_t icon, pipeline::AssetManager* assetManager) {
    if (icon >= kRaidTargetIconCount || !assetManager) return VK_NULL_HANDLE;

    static std::array<VkDescriptorSet, kRaidTargetIconCount> cache{};
    if (cache[icon]) return cache[icon];

    // Blizzard numbers the files 1-8 in the same order as the icon indices.
    const std::string path = "Interface\\TargetingFrame\\UI-RaidTargetingIcon_" +
                             std::to_string(icon + 1) + ".blp";
    // Only a successful upload is cached: a transient failure (descriptor pool
    // pressure) should be retried rather than blacklisting the icon for good.
    VkDescriptorSet ds = uploadUiTextureFromBlp(
        assetManager, path, core::Application::getInstance().getWindow());
    if (ds) cache[icon] = ds;
    return ds;
}

} // namespace ui
} // namespace wowee
