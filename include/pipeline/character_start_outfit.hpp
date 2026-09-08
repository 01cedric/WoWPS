#pragma once

#include "pipeline/dbc_loader.hpp"
#include <cstdint>
#include <vector>

namespace wowee::pipeline {

struct CharacterOutfitItem {
    uint32_t itemId = 0;
    uint32_t displayId = 0;
    uint8_t inventoryType = 0;
};

struct CharacterStartingOutfit {
    bool supportedLayout = false;
    bool matched = false;
    std::vector<CharacterOutfitItem> items;
};

// WotLK has 77 logical fields, but race/class/sex/outfit are four bytes,
// sharing word 1. Its record therefore occupies 296 bytes (74 words), NOT
// 77 * 4. Some DBC exporters report the word count or expand the four bytes
// to four words. Keep those layouts explicit so display IDs never come from
// the adjacent item-ID or inventory-type array.
inline CharacterStartingOutfit resolveCharacterStartingOutfit(
        const DBCFile* dbc, uint8_t race, uint8_t characterClass, uint8_t sex) {
    CharacterStartingOutfit result;
    if (!dbc || !dbc->isLoaded()) return result;
    const bool packed = dbc->getRecordSize() == 296 &&
        (dbc->getFieldCount() == 77 || dbc->getFieldCount() == 74);
    const bool expanded = dbc->getRecordSize() == 308 && dbc->getFieldCount() == 77;
    if (!packed && !expanded) return result;
    result.supportedLayout = true;
    const uint32_t key = uint32_t(race) | (uint32_t(characterClass) << 8) |
        (uint32_t(sex) << 16);
    for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
        if (packed) {
            if ((dbc->getUInt32(row, 1) & 0x00ffffffu) != key) continue;
        } else if (dbc->getUInt32(row, 1) != race ||
                   dbc->getUInt32(row, 2) != characterClass ||
                   dbc->getUInt32(row, 3) != sex) {
            continue;
        }
        result.matched = true;
        const uint32_t firstItem = packed ? 2u : 5u;
        for (uint32_t slot = 0; slot < 24; ++slot) {
            const int32_t item = dbc->getInt32(row, firstItem + slot);
            const int32_t display = dbc->getInt32(row, firstItem + 24 + slot);
            const int32_t type = dbc->getInt32(row, firstItem + 48 + slot);
            // Empty slots use zero or -1. Food and other inventory-only items
            // have no visible inventory type and are not preview equipment.
            if (item > 0 && display > 0 && type > 0 && type <= 28)
                result.items.push_back({uint32_t(item), uint32_t(display), uint8_t(type)});
        }
        break;
    }
    return result;
}

} // namespace wowee::pipeline
