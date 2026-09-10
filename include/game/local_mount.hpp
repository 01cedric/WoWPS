#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_auction_catalog.hpp"
namespace wowee::game {
// Explicit standalone ground-riding schedule; reputation discounts, flying
// ranks and class-specific training quests are separate unsupported scopes.
struct LocalRidingRank { uint16_t skill, previous; uint8_t level; uint32_t cost; const char* name; };
inline constexpr LocalRidingRank LocalRidingRanks[] = {
    {75,0,20,40000,"Apprentice Riding"}, {150,75,40,500000,"Journeyman Riding"}
};
inline uint16_t localMountRidingRequirement(const LocalSpellDefinition& spell) {
    return spell.mountSpeedPercent>60 ? 150 : 75;
}
inline bool localMountSupported(const LocalWorldContent& content,uint32_t itemId) {
    const auto* item=localAuctionMetadata(itemId);
    const auto* spell=item && item->mountSpell ? content.spell(item->mountSpell) : nullptr;
    return spell && spell->mountDisplayId && spell->unsupportedReason.empty();
}
inline const LocalSpellDefinition* localActiveMount(const LocalWorldContent& content,const LocalRealmPlayer& player) {
    const auto* spell=player.mountSpellId ? content.spell(player.mountSpellId) : nullptr;
    return spell && spell->mountDisplayId && spell->unsupportedReason.empty() ? spell : nullptr;
}
}
