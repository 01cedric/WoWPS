#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_cooldowns.hpp"
#include <algorithm>
#include <cstdio>

namespace wowee::ui {
inline uint32_t localActionCooldownMs(const game::LocalRealmPlayer& player, uint32_t spellId) {
    uint32_t remaining = player.globalCooldownMs;
    for (const auto& cooldown : player.cooldowns)
        if (cooldown.spellId == spellId) remaining = std::max(remaining, cooldown.remainingMs);
    return remaining;
}
inline uint32_t localActionCooldownMs(const game::LocalRealmPlayer& player,const game::LocalWorldContent& content,uint32_t spellId) {
    const auto* d=content.spell(spellId);
    return d?std::max(player.globalCooldownMs,game::localSpellCooldownRemaining(player,content,*d)):localActionCooldownMs(player,spellId);
}
inline void localActionCooldownText(uint32_t milliseconds, char (&text)[16]) {
    // Round upward so a spell still cooling down never displays zero.
    if (!milliseconds) text[0] = '\0';
    else if (milliseconds < 10000)
        std::snprintf(text, sizeof(text), "%.1f", float((milliseconds + 99) / 100) / 10.f);
    else
        std::snprintf(text, sizeof(text), "%u", milliseconds / 1000 + (milliseconds % 1000 != 0));
}
}
