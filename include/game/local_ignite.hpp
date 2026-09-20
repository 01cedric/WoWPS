#pragma once
#include "game/local_gameplay.hpp"
#include <algorithm>
#include <limits>
namespace wowee::game {
// Pinned AzerothCore spell_mage_ignite + Unit::CastDelayedSpellWithPeriodicAmount.
// Percentage is the rank's total damage (8..40%); the child has exactly two ticks.
// DamageInfo amount precedes HP clamping but follows absorption. Remainder uses
// the old BASE tick amount, before any caster periodic-damage modifiers.
inline uint32_t localIgniteTickAmount(uint32_t damage,uint32_t percent,
                                     uint32_t oldBaseTick=0,uint32_t remainingTicks=0) {
    if(percent<8||percent>40||percent%8)return 0;
    const uint64_t fresh=uint64_t(damage)*percent/200;
    const uint64_t carry=uint64_t(oldBaseTick)*std::min(remainingTicks,2u)/2;
    return uint32_t(std::min<uint64_t>(fresh+carry,std::numeric_limits<int32_t>::max()));
}
inline bool localIgniteScriptMatches(const LocalCombatEvent& event) {
    // DamageInfo must identify a spell. Molten Armor's fire retaliation is
    // expressly excluded by spell_mage_ignite::CheckProc, even when critical.
    return event.source&&event.target&&event.spell&&
        (event.kind==LocalCombatEventKind::SpellDamage||event.kind==LocalCombatEventKind::ProcDamage||
         event.kind==LocalCombatEventKind::PeriodicDamage||event.kind==LocalCombatEventKind::PlayerRanged)&&
        !(event.spellFamilyFlags[1]&8u);
}
inline constexpr uint32_t LocalIgniteApplicationDelayMs=400;
inline constexpr uint32_t LocalIgniteTickIntervalMs=2000;
inline constexpr uint32_t LocalIgniteDurationMs=4000;
inline bool validLocalIgniteProc(const LocalProcDefinition& p) {
    return p.effect==LocalProcEffect::Ignite&&p.spellId==12654&&p.flags==327680&&
        p.amount>=8&&p.amount<=40&&p.amount%8==0&&p.chance==100&&!p.charges&&!p.cooldownMs&&
        !p.ppm&&!p.amountPerLevel&&!p.baseLevel&&!p.maxLevel&&p.range==50000&&
        p.schoolMask==4&&p.spellFamily==3&&p.spellFamilyFlags==std::array<uint32_t,3>{134217728,0,8}&&
        p.triggerSchoolMask==4&&p.triggerSpellFamily==3&&p.triggerSpellFamilyFlags==std::array<uint32_t,3>{}&&
        p.hitMask==LocalProcHitCritical&&p.phaseMask==LocalProcPhaseHit&&p.spellTypeMask==1&&
        p.allowTriggered&&!p.pushbackPercent&&!p.requiredForms&&!p.resourceType&&!p.attributesMask&&
        !p.disableEffectsMask&&p.sourceEffectMask==1&&!p.hasUnsupportedConditions&&!p.hasUnsupportedScript;
}
}
