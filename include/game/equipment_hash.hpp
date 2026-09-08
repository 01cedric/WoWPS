#pragma once

/// A fingerprint of what a character is wearing, for caches that redraw when
/// it changes.
///
/// Two of them existed - the character screen's and the unit portrait's - each
/// with its own FNV-1a over the same three fields. They were not wrong, but
/// they were two answers to one question: which parts of an equipped item
/// change how it looks. Adding a field that does, and updating one, would
/// leave the other cache holding a picture of the previous outfit with nothing
/// to say so.
///
/// displayModel, inventoryType and enchantment are the three: the model worn,
/// the slot it occupies, and the visual the enchant puts on it. Anything else
/// on an equipped item - its id, its durability, its charges - changes nothing
/// a viewer can see, so it is deliberately not mixed in and a cache does not
/// redraw for it.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "game/character.hpp"

namespace wowee::game {

/// FNV-1a over the appearance-bearing fields of every equipped item, in slot
/// order. Order is part of it: the same items in different slots are a
/// different outfit.
inline uint64_t hashEquipmentAppearance(const std::vector<EquipmentItem>& equipment) {
    uint64_t h = 1469598103934665603ull;
    const auto mix8 = [&h](uint8_t b) {
        h ^= b;
        h *= 1099511628211ull;
    };
    const auto mix32 = [&mix8](uint32_t v) {
        mix8(static_cast<uint8_t>(v & 0xFF));
        mix8(static_cast<uint8_t>((v >> 8) & 0xFF));
        mix8(static_cast<uint8_t>((v >> 16) & 0xFF));
        mix8(static_cast<uint8_t>((v >> 24) & 0xFF));
    };
    for (const EquipmentItem& item : equipment) {
        mix32(item.displayModel);
        mix8(item.inventoryType);
        mix32(item.enchantment);
    }
    return h;
}

}  // namespace wowee::game
