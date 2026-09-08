#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_auction_catalog.hpp"
namespace wowee::game {
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
