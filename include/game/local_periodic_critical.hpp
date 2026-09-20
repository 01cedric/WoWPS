#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_melee.hpp"
#include "game/local_talents.hpp"
#include <algorithm>
#include <cstdint>

// P03/D3: conditional periodic criticals.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   SpellAuraEffects.cpp::AuraEffect::CalcPeriodicCritChance (1073-1104) -
//     a periodic effect can only crit when the caster carries an aura of type
//     SPELL_AURA_ABILITY_PERIODIC_CRIT (**286**, not 277) whose family and class
//     mask affect this spell. Only then is the caster's own spell critical
//     chance taken. Absent such an aura the chance is zero, full stop.
//   SpellAuraEffects.cpp::AuraEffect::IsAffectedOnSpell (1106-1116) - the
//     granting aura's spell family must match and one of its three class-mask
//     words must intersect the affected spell's family flags.
//   SpellAuraEffects.cpp::CalculatePeriodicData (584-597) - the chance is
//     snapshotted when the aura is applied, never recomputed per tick.
//
// Scope, stated plainly: **no granting aura is importable at this baseline.**
// All fourteen aura-286 rows in the supplied client are unimported and the three
// talents that would grant one sit behind unreachable tiers, so this mechanism
// has no reachable consumer yet. It is implemented, snapshotted and tested; it
// is not claimed to be reachable. See the source audit.
namespace wowee::game {

/// AuraEffect::IsAffectedOnSpell for a periodic-crit grant.
inline bool localPeriodicCritGrantAffects(const LocalSpellDefinition& grant,
                                          const LocalSpellDefinition& periodic) {
    if(!grant.periodicCritFamily||grant.periodicCritFamily!=periodic.spellFamily)return false;
    for(unsigned k=0;k<3;++k)
        if(grant.periodicCritMask[k]&periodic.spellFamilyFlags[k])return true;
    return false;
}

/// CalcPeriodicCritChance. Returns basis points, zero when the caster carries no
/// granting aura that affects this spell. A missing or departed caster yields
/// zero, exactly as the reference's null-caster branch does.
inline uint16_t localPeriodicCritChanceBasisPoints(const LocalRealmPlayer* caster,
                                                   const LocalWorldContent& content,
                                                   const LocalSpellDefinition& periodic) {
    if(!caster)return 0;
    bool granted=false;
    for(const auto& aura:caster->statAuras) {
        if(!aura.remainingMs)continue;
        const auto* d=content.spell(aura.spellId);
        if(d&&localPeriodicCritGrantAffects(*d,periodic)){granted=true;break;}
    }
    if(!granted&&validLocalTalents(*caster))
        for(const auto& [talentId,rank]:caster->talents) {
            const auto* d=localTalentSpell(content,talentId,rank);
            if(d&&localPeriodicCritGrantAffects(*d,periodic)){granted=true;break;}
        }
    if(!granted)return 0;
    const float chance=localSpellCritChance(*caster,content,periodic.schoolMask);
    if(!std::isfinite(chance)||chance<=0)return 0;
    return uint16_t(std::clamp(int(chance*100.f),0,10000));
}
}
