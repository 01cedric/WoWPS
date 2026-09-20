#pragma once
#include "game/local_talents.hpp"
#include "game/local_combat_reach.hpp"
#include <cmath>
namespace wowee::game {
inline float localSpellMaximumRange(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d) {
    if(!std::isfinite(d.range)||d.range<=0)return 0;
    const auto flat=localTalentCastModifier(p,c,d,5,false);
    const auto percent=localTalentCastModifier(p,c,d,5,true);
    return std::min(100.0f,(d.range+float(flat))*(100+percent)/100.0f);
}
/// The minimum-range half of Spell::CheckRange (:7369-7376): a
/// SPELL_RANGE_RANGED row is too close inside min_range + GetMeleeRange(target)
/// (IsWithinRange, a `<=` test, Unit.cpp:805-818); any other row with a positive
/// minimum is too close inside IsWithinCombatRange(min_range), which adds both
/// reaches; a row with no minimum is never too close.
inline bool localSpellTargetTooClose(const LocalSpellDefinition& d,float squared,float targetCombatReach) {
    const float reach=std::isfinite(targetCombatReach)&&targetCombatReach>0?targetCombatReach:kLocalWorldObjectSize;
    if(!std::isfinite(squared))return false;
    if(d.sourceRangeFlags==2) {
        const float minimum=d.minRange+localMeleeRange(kLocalDefaultCombatReach,reach);
        return squared<=minimum*minimum;
    }
    return d.minRange>0&&localWithinCombatRange(squared,d.minRange,kLocalDefaultCombatReach,reach);
}
/// Spell::CheckRange, Spell.cpp:7301-7396, for a unit target that is not the
/// caster, with `targetCombatReach` the target's UNIT_FIELD_COMBATREACH
/// (kLocalDefaultCombatReach for a player, localCreatureCombatReach for a
/// catalog creature) and `strict` true at cast start, false at completion.
///
/// previously this compared the raw imported column against the centre-to-centre
/// distance and added nothing, which is the audit's D23 / D24 / D25. The four
/// arms below are the reference's, in its order:
///   - a "Self Only" row (RangeEntry->ID == 1, the only row in the client's
///     SpellRange.dbc whose two maxima are both zero) returns SPELL_CAST_OK
///     before anything is measured (:7314-7315);
///   - a non-melee row gains min(3, 10 %) at completion (:7332-7333);
///   - a melee row asks IsWithinMeleeRange(max_range - 2 * MIN_MELEE_REACH),
///     whose own maximum is max(reachA + reachB + 4/3, 5) (:7345-7356);
///   - any other row asks IsWithinCombatRange(max_range), which adds both
///     reaches (:7358);
///   - the minimum is min_range + GetMeleeRange for a SPELL_RANGE_RANGED row
///     and IsWithinCombatRange(min_range) otherwise (:7369-7376).
/// The SPELLMOD_RANGE application (:7328-7329) is localSpellMaximumRange's.
inline bool localSpellTargetInRange(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d,
                                    uint32_t map,uint32_t instance,float x,float y,float z,
                                    float targetCombatReach=kLocalDefaultCombatReach,bool strict=true) {
    if(p.mapId!=map||p.instanceId!=instance||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
    auto maximum=localSpellMaximumRange(p,c,d);
    if(maximum<=0)return true;
    const auto dx=p.x-x,dy=p.y-y,dz=p.z-z;const auto squared=dx*dx+dy*dy+dz*dz;
    if(!std::isfinite(squared))return false;
    const float reach=std::isfinite(targetCombatReach)&&targetCombatReach>0?targetCombatReach:kLocalWorldObjectSize;
    const bool melee=d.sourceRangeFlags==1;
    if(!melee&&!strict)maximum+=std::min(3.0f,maximum*0.1f);
    if(melee) {
        const float real=std::max(0.0f,maximum-2.0f*kLocalMinMeleeReach);
        if(!localWithinMeleeRange(squared,kLocalDefaultCombatReach,reach,real))return false;
    } else if(!localWithinCombatRange(squared,maximum,kLocalDefaultCombatReach,reach))return false;
    return !localSpellTargetTooClose(d,squared,reach);
}
}
