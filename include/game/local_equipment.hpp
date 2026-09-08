#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wowee::game {

// Save v5 and LAN wire v4 use the WotLK equipment order. Bag containers are
// separate from these nineteen worn slots.
enum class LocalEquipmentSlot : uint8_t {
    Head, Neck, Shoulders, Shirt, Chest, Waist, Legs, Feet, Wrists, Hands,
    Finger1, Finger2, Trinket1, Trinket2, Back, MainHand, OffHand, Ranged, Tabard
};
inline constexpr size_t kLocalEquipmentSlotCount = 19;
inline constexpr size_t localEquipmentIndex(LocalEquipmentSlot slot) { return static_cast<size_t>(slot); }
inline constexpr std::array<size_t, 4> kLegacyLocalEquipmentSlots = {15, 4, 6, 7};
inline constexpr std::array<const char*, kLocalEquipmentSlotCount> kLocalEquipmentSlotNames = {
    "Head", "Neck", "Shoulders", "Shirt", "Chest", "Waist", "Legs", "Feet", "Wrists", "Hands",
    "Finger 1", "Finger 2", "Trinket 1", "Trinket 2", "Back", "Main hand", "Off hand", "Ranged", "Tabard"
};

inline constexpr uint32_t localEquipmentSlotBit(size_t slot) {
    return slot < kLocalEquipmentSlotCount ? uint32_t(1) << slot : 0;
}

// inventoryType is authoritative whenever present. Legacy schema-1 fixtures
// without it retain their explicit 1=weapon, 2=chest, 3=legs, 4=feet meaning.
// A real non-wearable type must never fall back to a legacy adapted slot.
inline constexpr uint32_t localEquipmentSlotMask(uint8_t inventoryType, uint8_t legacySlot = 0) {
    switch (inventoryType) {
        case 1: return localEquipmentSlotBit(0);
        case 2: return localEquipmentSlotBit(1);
        case 3: return localEquipmentSlotBit(2);
        case 4: return localEquipmentSlotBit(3);
        case 5: case 20: return localEquipmentSlotBit(4);
        case 6: return localEquipmentSlotBit(5);
        case 7: return localEquipmentSlotBit(6);
        case 8: return localEquipmentSlotBit(7);
        case 9: return localEquipmentSlotBit(8);
        case 10: return localEquipmentSlotBit(9);
        case 11: return localEquipmentSlotBit(10) | localEquipmentSlotBit(11);
        case 12: return localEquipmentSlotBit(12) | localEquipmentSlotBit(13);
        case 13: return localEquipmentSlotBit(15) | localEquipmentSlotBit(16);
        case 14: case 22: case 23: return localEquipmentSlotBit(16);
        case 15: case 25: case 26: case 28: return localEquipmentSlotBit(17);
        case 16: return localEquipmentSlotBit(14);
        case 17: case 21: return localEquipmentSlotBit(15);
        case 19: return localEquipmentSlotBit(18);
        case 0: return legacySlot >= 1 && legacySlot <= kLegacyLocalEquipmentSlots.size()
            ? localEquipmentSlotBit(kLegacyLocalEquipmentSlots[legacySlot - 1]) : 0;
        default: return 0; // Bags, ammo, quivers and unknown inventory types.
    }
}

inline constexpr bool localEquipmentFits(uint8_t inventoryType, uint8_t legacySlot, size_t slot) {
    return (localEquipmentSlotMask(inventoryType, legacySlot) & localEquipmentSlotBit(slot)) != 0;
}

} // namespace wowee::game
