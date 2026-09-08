#pragma once
#include "game/character.hpp"
#include "game/inventory.hpp"

namespace wowee::rendering {

// Character-enumeration equipment uses real slots. InventoryType alone cannot
// distinguish two one-handed weapons, or a lone weapon held in the off hand.
inline const game::EquipmentItem* previewEquipmentForHand(
        const std::vector<game::EquipmentItem>& equipment, bool offHand) {
    const auto at = [&](game::EquipSlot slot) -> const game::EquipmentItem* {
        const size_t index = static_cast<size_t>(slot);
        return index < equipment.size() && equipment[index].displayModel ? &equipment[index] : nullptr;
    };
    const auto* main = at(game::EquipSlot::MAIN_HAND);
    if (offHand) {
        if (main && main->inventoryType == game::InvType::TWO_HAND) return nullptr;
        const auto* off = at(game::EquipSlot::OFF_HAND);
        if (off && (off->inventoryType == game::InvType::ONE_HAND || off->inventoryType == game::InvType::SHIELD ||
                    off->inventoryType == game::InvType::OFF_HAND || off->inventoryType == game::InvType::HOLDABLE)) return off;
        return nullptr;
    }
    if (main && (main->inventoryType == game::InvType::ONE_HAND || main->inventoryType == game::InvType::TWO_HAND ||
                 main->inventoryType == game::InvType::MAIN_HAND)) return main;
    const auto* ranged = at(game::EquipSlot::RANGED);
    return ranged && (ranged->inventoryType == game::InvType::RANGED_BOW ||
                      ranged->inventoryType == game::InvType::THROWN ||
                      ranged->inventoryType == game::InvType::RANGED_GUN) ? ranged : nullptr;
}
} // namespace wowee::rendering
