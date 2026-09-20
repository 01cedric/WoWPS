#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>

namespace wowee::game {
// Complete, single-row SmartAI UPDATE_IC -> CAST(VICTIM) profiles only.
// No event phases, links, conditions, cast flags or resource costs are omitted.
struct LocalNpcSpellProfile {
    uint32_t entry, spellId, initialMinMs, initialMaxMs, repeatMinMs, repeatMaxMs;
};
inline constexpr LocalNpcSpellProfile kLocalNpcSpellProfiles[] = {
#include "game/local_npc_spell_profiles_generated.inc"
};
inline const LocalNpcSpellProfile* localNpcSpellProfile(uint32_t entry) {
    const auto* p=std::lower_bound(std::begin(kLocalNpcSpellProfiles),std::end(kLocalNpcSpellProfiles),entry,
        [](const auto& profile,uint32_t id){return profile.entry<id;});
    return p!=std::end(kLocalNpcSpellProfiles)&&p->entry==entry?p:nullptr;
}
// SpellEffectInfo::CalcValue: creature-only SCALES_WITH_CREATURE_LEVEL uses
// BaseDamage at creature level divided by BaseDamage at SpellLevel. Both
// reviewed Fireball casters have unit_class1/expansion0. No player SP term.
inline float localNpcSpellDamageScale(uint32_t entry,uint32_t level,uint32_t spellId) {
    const auto* profile=localNpcSpellProfile(entry);
    if(!profile||profile->spellId!=spellId||level<1||level>83)return 0.f;
    if(spellId==5401)return 1.f; // Explicit EffectRealPointsPerLevel instead.
    static constexpr float baseDamage[]={
#include "game/local_npc_spell_scaling_generated.inc"
    };
    static_assert(std::size(baseDamage)==84);
    return baseDamage[level]/baseDamage[20];
}
}
