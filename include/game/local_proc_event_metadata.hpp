#pragma once
#include "game/local_gameplay.hpp"

namespace wowee::game {
// AzerothCore 9c416aa SpellMgr.h SPELL_PROC_FLAG_MASK includes ranged
// auto shots, but excludes melee auto swings and the generic TAKEN_DAMAGE bit.
constexpr uint32_t LocalProcSpellEventFlags=0x2ffff0u;

inline LocalCombatAttackType localProcSpellAttackType(uint8_t damageClass) {
    switch(damageClass) {
        case 0:return LocalCombatAttackType::None;
        case 1:return LocalCombatAttackType::Magic;
        case 2:return LocalCombatAttackType::Melee;
        case 3:return LocalCombatAttackType::Ranged;
        default:return LocalCombatAttackType::Unspecified;
    }
}

// Keep producer overrides (including ranged auto shots) and synthetic leaves
// intact. A retained client row supplies classification even without a custom
// gameplay profile: damage class zero is an explicit None, not missing data.
inline void localHydrateProcEventMetadata(LocalCombatEvent& event,const LocalSpellDefinition* definition) {
    // Aura::GetProcEffectMask checks this child-spell attribute independently
    // of the attack type. Never infer it from synthetic proc observations.
    if(definition&&definition->clientSpell) {
        event.sourceNotAProc=definition->sourceNotAProc;
        event.sourceDoNotConsumeResources=definition->sourceDoNotConsumeResources;
        event.sourceHasManaCost=definition->mana||definition->manaPercent;
        event.sourceRangedAuto=definition->rangedAutoProfile!=0;
    }
    if(event.attackType!=LocalCombatAttackType::Unspecified)return;
    if(event.kind==LocalCombatEventKind::PlayerRanged||event.sourceRangedAuto) {
        event.attackType=LocalCombatAttackType::Ranged;
        return;
    }
    if(event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee||
       event.kind==LocalCombatEventKind::PetMelee) {
        event.attackType=LocalCombatAttackType::Melee;
        return;
    }
    if(definition&&definition->clientSpell)
        event.attackType=localProcSpellAttackType(definition->sourceDamageClass);
}

// Unit::ProcSkillsAndAuras uses pre-overheal HealInfo::GetHeal, not effective
// health gain. DamageInfo includes absorbed damage but excludes blocked damage.
// SpellMgr applies this mask only for LocalProcSpellEventFlags above.
inline uint8_t localProcEventSpellTypeMask(const LocalCombatEvent& event) {
    if(event.reflectionOnly)return 4;
    if(event.kind==LocalCombatEventKind::SpellCast||event.kind==LocalCombatEventKind::SpellFinish)return 7;
    const bool heal=event.kind==LocalCombatEventKind::DirectHeal||
        event.kind==LocalCombatEventKind::PeriodicHeal||event.kind==LocalCombatEventKind::ProcHeal;
    if(heal&&event.attempted>event.absorbed)return 2;
    const bool damage=event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee||
        event.kind==LocalCombatEventKind::PetMelee||event.kind==LocalCombatEventKind::PlayerRanged||
        event.kind==LocalCombatEventKind::SpellDamage||event.kind==LocalCombatEventKind::PeriodicDamage||
        event.kind==LocalCombatEventKind::ProcDamage;
    const bool avoided=event.outcome==LocalMeleeOutcome::Miss||event.outcome==LocalMeleeOutcome::Dodge||
        event.outcome==LocalMeleeOutcome::Parry;
    if(damage&&!avoided&&(event.effective||event.absorbed||event.attempted>event.blocked))return 1;
    return event.spell?4:0;
}
}
