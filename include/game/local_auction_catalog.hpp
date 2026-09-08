#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace wowee::game {
// Server-authored item fields absent from Item.dbc. These records do not load
// models, create players or retain a decoded copy of the world catalog.
struct LocalAuctionItemMetadata {
    uint32_t id, sellPrice, buyPrice, allowableClasses, allowableRaces, mountSpell;
    // Highest explicit, ungrouped creature-drop probability (1/100 percent).
    // Zero means unknown: grouped/reference chances are not global drop rates.
    uint16_t stack, dropChanceBp;
    uint8_t itemClass, subClass, quality, requiredLevel, bonding, flags;
    enum : uint8_t { Material = 1, Mount = 2, DropMount = 4, TcgMount = 8, Supply = 16 };
    bool has(uint8_t flag) const { return (flags & flag) != 0; }
    bool tradeable() const { return bonding != 1 && bonding != 4 && itemClass != 12 && quality < 7; }
};
inline constexpr LocalAuctionItemMetadata kLocalAuctionItems[] = {
#include "game/local_auction_items_generated.inc"
};
inline const LocalAuctionItemMetadata* localAuctionMetadata(uint32_t id) {
    const auto first = std::begin(kLocalAuctionItems), last = std::end(kLocalAuctionItems);
    const auto it = std::lower_bound(first, last, id,
        [](const LocalAuctionItemMetadata& item, uint32_t entry) { return item.id < entry; });
    return it != last && it->id == id ? it : nullptr;
}
} // namespace wowee::game
