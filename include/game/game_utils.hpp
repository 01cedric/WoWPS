#pragma once

#include "game/expansion_profile.hpp"
#include "game/item_text.hpp"
#include "core/application.hpp"

namespace wowee {
namespace game {

inline bool isActiveExpansion(const char* expansionId) {
    auto& app = core::Application::getInstance();
    auto* registry = app.getExpansionRegistry();
    if (!registry) return false;
    auto* profile = registry->getActive();
    if (!profile) return false;
    return profile->id == expansionId;
}

inline bool isClassicLikeExpansion() {
    return isActiveExpansion("classic") || isActiveExpansion("turtle");
}

inline bool isPreWotlk() {
    return isClassicLikeExpansion() || isActiveExpansion("tbc");
}

// Shared item link formatter used by inventory, quest, spell, and social
// handlers. It is a second name for itemChatLink and not a second copy: this
// one carried its own quality table, which is how the colours came to be
// written out in six places for one set of eight values.
inline std::string buildItemLink(uint32_t itemId, uint32_t quality, const std::string& name) {
    return itemChatLink(itemId, quality, name);
}

} // namespace game
} // namespace wowee
