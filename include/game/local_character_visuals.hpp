#pragma once

#include "game/character.hpp"
#include "game/inventory.hpp"
#include "game/local_gameplay.hpp"
#include <algorithm>

namespace wowee::game {

inline constexpr std::array<EquipSlot, kLocalEquipmentSlotCount> kLocalEquipmentVisualSlots = {
    EquipSlot::HEAD, EquipSlot::NECK, EquipSlot::SHOULDERS, EquipSlot::SHIRT, EquipSlot::CHEST,
    EquipSlot::WAIST, EquipSlot::LEGS, EquipSlot::FEET, EquipSlot::WRISTS, EquipSlot::HANDS,
    EquipSlot::RING1, EquipSlot::RING2, EquipSlot::TRINKET1, EquipSlot::TRINKET2,
    EquipSlot::BACK, EquipSlot::MAIN_HAND, EquipSlot::OFF_HAND, EquipSlot::RANGED, EquipSlot::TABARD};

enum class LocalEquipmentSource { SavedInventory, AuthoritySnapshot };

inline const LocalItemDefinition* localEquippedItem(
        const LocalRealmPlayer& player, const LocalWorldContent* content, size_t slot,
        LocalEquipmentSource source = LocalEquipmentSource::SavedInventory) {
    if (!content || slot >= player.equipment.size() || !player.equipment[slot]) return nullptr;
    const auto id = player.equipment[slot];
    uint32_t owned = 0, reserved = 0;
    for (const auto& stack : player.inventory) if (stack.itemId == id) owned += stack.count;
    for (size_t i = 0; i <= slot; ++i) if (player.equipment[i] == id) ++reserved;
    // Public LAN snapshots carry the authority's equipped IDs, not private
    // inventory stacks. Saved-file previews can and should check both.
    if (source == LocalEquipmentSource::SavedInventory && owned < reserved) return nullptr;
    const auto* item = content->item(id);
    if (!item || !localEquipmentFits(item->inventoryType, item->slot, slot)) return nullptr;
    if (slot == localEquipmentIndex(LocalEquipmentSlot::OffHand)) {
        const auto* main = content->item(player.equipment[localEquipmentIndex(LocalEquipmentSlot::MainHand)]);
        if (main && main->inventoryType == 17) return nullptr;
    }
    return item;
}

// Used both for character selection and live realm snapshots. Neither path
// should erase appearance bytes, nor substitute a race/class starter outfit
// for the items this saved character actually owns and has equipped.
inline Character localCharacterVisual(const LocalRealmPlayer& player,
                                      const LocalWorldContent* content = nullptr,
                                      LocalEquipmentSource source = LocalEquipmentSource::SavedInventory) {
    Character c{};
    c.guid = player.guid;
    c.name = player.name;
    c.race = static_cast<Race>(player.race);
    c.characterClass = static_cast<Class>(player.classId);
    c.gender = static_cast<Gender>(player.gender);
    c.level = player.level;
    c.mapId = player.mapId;
    c.x = player.x; c.y = player.y; c.z = player.z;
    c.useFemaleModel = player.useFemaleModel;
    c.appearanceBytes = uint32_t(player.skin) | (uint32_t(player.face) << 8) |
                       (uint32_t(player.hairStyle) << 16) | (uint32_t(player.hairColor) << 24);
    c.facialFeatures = player.facialHair;
    c.equipment.resize(static_cast<size_t>(EquipSlot::NUM_SLOTS));
    for (size_t slot = 0; slot < player.equipment.size(); ++slot) {
        const auto* item = localEquippedItem(player, content, slot, source);
        if (!item || !item->displayId || !item->inventoryType || item->inventoryType > 28) continue;
        c.equipment[static_cast<size_t>(kLocalEquipmentVisualSlots[slot])] =
            {item->displayId, item->inventoryType, 0};
    }
    return c;
}

} // namespace wowee::game
