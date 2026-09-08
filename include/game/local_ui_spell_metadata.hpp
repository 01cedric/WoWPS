#pragma once
#include "game/local_gameplay.hpp"
#include <unordered_map>
namespace wowee::game {
template<class Entry>
std::unordered_map<uint32_t,Entry> localUiSpellMetadata(const LocalWorldContent& content) {
    std::unordered_map<uint32_t,Entry> result;
    result.reserve(content.spells.size()+1);
    for(const auto& spell:content.spells) {
        Entry entry;
        entry.name=spell.name;
        entry.description=spell.unsupportedReason;
        entry.schoolMask=spell.schoolMask;
        entry.maxRange=spell.range;
        entry.durationSec=spell.durationMs/1000.0f;
        entry.spellVisualId=spell.visualId;
        entry.recoveryMs=spell.cooldownMs;
        entry.categoryRecoveryMs=spell.globalCooldownMs;
        result.emplace(spell.id,std::move(entry));
    }
    if(!result.count(6603)) { Entry attack;attack.name="Attack";
        attack.maxRange=5.0f;attack.schoolMask=1;result.emplace(6603,std::move(attack)); }
    return result;
}
}
