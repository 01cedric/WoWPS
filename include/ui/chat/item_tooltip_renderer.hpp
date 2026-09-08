#pragma once

#include <cstdint>

namespace wowee {
namespace game { class GameHandler; }
namespace pipeline { class AssetManager; }
namespace ui {

class InventoryScreen;

/**
 * Renders a full WoW-style item tooltip via ImGui.
 *
 * Extracted from ChatMarkupRenderer::renderItemTooltip (Phase 6.7).
 * Handles: item name/quality color, armor/DPS, stats, sockets, set bonuses,
 * durability, sell price, required level, comparison tooltip.
 */
class ItemTooltipRenderer {
public:
    static void render(uint32_t itemEntry,
                       game::GameHandler& gameHandler,
                       InventoryScreen& inventoryScreen,
                       pipeline::AssetManager* assetMgr);
};

} // namespace ui
} // namespace wowee
