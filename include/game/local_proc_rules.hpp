#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_proc_event_metadata.hpp"
#include "game/local_proc_attributes.hpp"
#include "game/local_ignite.hpp"
#include <algorithm>
#include <cmath>

namespace wowee::game {
inline bool validLocalProcFilters(const LocalProcDefinition& p) {
    // Only actual local event producers are admitted. Kill/killed/death are
    // distinct lifecycle events; unimplemented trap activation stays rejected.
    constexpr uint32_t supportedFlags=0x1dfffffu;
    return validLocalProcAttributes(p) && !(p.flags&~supportedFlags) && p.triggerSchoolMask<=127 && p.triggerSpellFamily<=1000 &&
        (!(p.triggerSpellFamilyFlags[0]|p.triggerSpellFamilyFlags[1]|p.triggerSpellFamilyFlags[2])||p.triggerSpellFamily) &&
        !(p.hitMask&~LocalProcSupportedHits) && !(p.phaseMask&~7u) &&
        !(p.spellTypeMask&~7u) &&
        std::isfinite(p.ppm) && p.ppm>=0 && p.ppm<=100 && p.chance<=100;
}
inline uint32_t localProcEventFlags(const LocalCombatEvent& event,uint64_t ownerGuid) {
    if(!ownerGuid || (ownerGuid!=event.source&&ownerGuid!=event.target))return 0;
    if(event.kind==LocalCombatEventKind::ProcMana||event.kind==LocalCombatEventKind::ProcPower||event.kind==LocalCombatEventKind::ProcCombo||event.kind==LocalCombatEventKind::ProcAura)return 0; // Resource/aura observations are not damage/heal events.
    if(event.kind==LocalCombatEventKind::Kill)return (ownerGuid==event.source?0x2u:0u)|(ownerGuid==event.target?0x1u:0u);
    if(event.kind==LocalCombatEventKind::Death)return ownerGuid==event.source?0x1000000u:0u;
    if(event.reflectionOnly)return ownerGuid==event.target?0x20000u:0u;
    uint32_t done=0,taken=0;
    const bool periodic=event.kind==LocalCombatEventKind::PeriodicDamage||event.kind==LocalCombatEventKind::PeriodicHeal;
    const bool positive=event.kind==LocalCombatEventKind::DirectHeal||event.kind==LocalCombatEventKind::PeriodicHeal||
        event.kind==LocalCombatEventKind::ProcHeal||((event.kind==LocalCombatEventKind::SpellCast||event.kind==LocalCombatEventKind::SpellHit||event.kind==LocalCombatEventKind::SpellFinish)&&event.positiveSpell);
    const bool swing=event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee||
        event.kind==LocalCombatEventKind::PetMelee||event.kind==LocalCombatEventKind::PlayerRanged||event.sourceRangedAuto;
    const auto type=event.attackType==LocalCombatAttackType::Unspecified?
        (event.kind==LocalCombatEventKind::PlayerRanged?LocalCombatAttackType::Ranged:swing?LocalCombatAttackType::Melee:LocalCombatAttackType::Magic):event.attackType;
    if(periodic){done=0x40000;taken=0x80000;}
    else if(type==LocalCombatAttackType::Melee){done=swing?0x4:0x10;taken=swing?0x8:0x20;}
    else if(type==LocalCombatAttackType::Ranged){done=swing?0x40:0x100;taken=swing?0x80:0x200;}
    else if(type==LocalCombatAttackType::None){done=positive?0x400:0x1000;taken=positive?0x800:0x2000;}
    else if(type==LocalCombatAttackType::Magic){done=positive?0x4000:0x10000;taken=positive?0x8000:0x20000;}
    uint32_t flags=ownerGuid==event.source?done:0;
    if(ownerGuid==event.source&&type==LocalCombatAttackType::Melee&&!periodic&&!positive&&event.kind!=LocalCombatEventKind::SpellCast&&event.kind!=LocalCombatEventKind::SpellFinish)
        flags|=event.offHand?0x800000:0x400000;
    // Cast completion is a source event; its recipient has not yet been hit.
    if(ownerGuid==event.target&&event.kind!=LocalCombatEventKind::SpellCast&&event.kind!=LocalCombatEventKind::SpellFinish){
        flags|=taken;
        if(!positive&&event.effective)flags|=0x100000; // Actual damage taken.
    }
    return flags;
}
inline uint32_t localProcEventHitMask(const LocalCombatEvent& event) {
    if(event.reflectionOnly)return LocalProcHitReflect;
    if(event.kind==LocalCombatEventKind::SpellCast||event.kind==LocalCombatEventKind::SpellHit)return LocalProcHitNormal;
    if(event.kind==LocalCombatEventKind::SpellFinish)return LocalProcHitNormal|(event.outcome==LocalMeleeOutcome::Critical?LocalProcHitCritical:0u);
    uint32_t mask=(event.absorbed?LocalProcHitAbsorb:0u)|(event.blocked?LocalProcHitBlock:0u);
    if((event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee||
        event.kind==LocalCombatEventKind::PetMelee||event.kind==LocalCombatEventKind::PlayerRanged)&&
       event.blocked&&event.attempted<=event.blocked&&!event.effective)mask|=LocalProcHitFullBlock;
    switch(event.outcome){
        case LocalMeleeOutcome::Reflect:return LocalProcHitReflect;
        case LocalMeleeOutcome::Miss:return mask|LocalProcHitMiss;
        case LocalMeleeOutcome::Dodge:return mask|LocalProcHitDodge;
        case LocalMeleeOutcome::Parry:return mask|LocalProcHitParry;
        // A non-NONE SpellMissInfo yields only its own bit: DamageInfo never
        // adds NORMAL or CRITICAL on top of an avoidance result.
        case LocalMeleeOutcome::Resist:return mask|LocalProcHitFullResist;
        case LocalMeleeOutcome::Immune:return mask|LocalProcHitImmune;
        case LocalMeleeOutcome::Deflect:return mask|LocalProcHitDeflect;
        case LocalMeleeOutcome::Block:mask|=LocalProcHitBlock;break;
        default:break;
    }
    // A completely absorbed/blocked hit retains its explicit outcome, without
    // pretending health changed. Overheal still records a real attempted heal.
    if(event.effective || event.attempted>uint64_t(event.absorbed)+event.blocked ||
       (event.kind==LocalCombatEventKind::SpellDamage&&event.attackType==LocalCombatAttackType::Melee))
        mask|=event.outcome==LocalMeleeOutcome::Critical?LocalProcHitCritical:LocalProcHitNormal;
    return mask;
}
inline bool localProcMatches(const LocalProcDefinition& p,const LocalCombatEvent& event,uint64_t ownerGuid) {
    const uint32_t flags=localProcEventFlags(event,ownerGuid);
    constexpr uint32_t requiredPhaseFlags=0x255550u; // SPELL_PROC_FLAG_MASK & DONE_HIT_PROC_FLAG_MASK
    if(!validLocalProcFilters(p)||!(p.flags&flags))return false;
    // SpellMgr accepts lifecycle flags after XP/cost attributes, before all
    // ordinary hit, school, spell-type and family checks. Runtime applies
    // localProcAttributeEligible with its actual aura application context.
    if(flags&0x1000003u)return true;
    if((flags&requiredPhaseFlags)&&!(p.phaseMask&(event.kind==LocalCombatEventKind::SpellCast?LocalProcPhaseCast:
        event.kind==LocalCombatEventKind::SpellFinish?LocalProcPhaseFinish:LocalProcPhaseHit)))return false;
    constexpr uint32_t takenHitFlags=0x1aaaa8u,doneHitFlags=0xe55554u;
    if((flags&takenHitFlags)||((flags&doneHitFlags)&&(event.kind!=LocalCombatEventKind::SpellCast||p.hitMask))) {
        const uint32_t hitMask=p.hitMask?p.hitMask:LocalProcHitNormal|LocalProcHitCritical|
            ((flags&takenHitFlags)?0u:LocalProcHitAbsorb);
        if(!(hitMask&localProcEventHitMask(event)))return false;
    }
    // SpellMgr filters spell type only for spell/periodic/ranged-auto flags;
    // plain melee auto attacks do not carry a spell-type qualification.
    if(p.spellTypeMask&&(localProcEventFlags(event,ownerGuid)&LocalProcSpellEventFlags)&&
       !(p.spellTypeMask&localProcEventSpellTypeMask(event)))return false;
    const bool triggered=event.procDepth||event.auraSpell||event.kind==LocalCombatEventKind::ProcDamage||
        event.kind==LocalCombatEventKind::ProcMana||event.kind==LocalCombatEventKind::ProcPower||event.kind==LocalCombatEventKind::ProcCombo||event.kind==LocalCombatEventKind::ProcHeal;
    // Aura::GetProcEffectMask exempts all auto attacks, including ranged
    // auto shots, from the generic triggered-spell exclusion.
    const bool autoAttack=(flags&0xccu)!=0;
    if(triggered&&!p.allowTriggered&&!(p.attributesMask&LocalProcTriggeredCanProc)&&!autoAttack&&!event.sourceNotAProc)return false;
    if(p.effect==LocalProcEffect::ConsumeOwnerAuraCharge && p.spellFamily==11 &&
       (event.spell==17364||event.spell==60103||(event.spellFamily==11&&(event.spellFamilyFlags[0]&0x00800000))))return false;
    if(p.effect==LocalProcEffect::ApplyOwnerAura&&p.spellId==16870&&event.spell&&!event.omenProcEligible)return false;
    if(p.triggerSchoolMask&&!(p.triggerSchoolMask&event.schoolMask))return false;
    if(!(flags&LocalProcSpellEventFlags)||!event.spell)return true;
    if(p.triggerSpellFamily&&p.triggerSpellFamily!=event.spellFamily)return false;
    const auto& mask=p.triggerSpellFamilyFlags;
    return !(mask[0]|mask[1]|mask[2]) || (mask[0]&event.spellFamilyFlags[0]) ||
        (mask[1]&event.spellFamilyFlags[1]) || (mask[2]&event.spellFamilyFlags[2]);
}
inline uint32_t localProcChanceBasisPoints(const LocalProcDefinition& p,const LocalCombatEvent& event) {
    if(!validLocalProcFilters(p))return 0;
    if(!p.ppm)return uint32_t(p.chance)*100;
    const bool swing=event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee||
        event.kind==LocalCombatEventKind::PetMelee||event.kind==LocalCombatEventKind::PlayerRanged;
    const auto type=event.attackType==LocalCombatAttackType::Unspecified?
        (event.kind==LocalCombatEventKind::PlayerRanged?LocalCombatAttackType::Ranged:swing?LocalCombatAttackType::Melee:LocalCombatAttackType::Magic):event.attackType;
    if(p.effect==LocalProcEffect::ApplyOwnerAura&&p.spellId==16870&&event.spell&&type==LocalCombatAttackType::Magic&&
       (event.kind==LocalCombatEventKind::SpellDamage||event.kind==LocalCombatEventKind::DirectHeal||event.kind==LocalCombatEventKind::SpellHit)) {
        if(event.sourceRawCastTimeMs>600000)return 0;
        return uint32_t(std::clamp(std::llround(double(p.ppm)*std::max(1500u,event.sourceRawCastTimeMs)/6.0),0LL,10000LL));
    }
    if((type!=LocalCombatAttackType::Melee&&type!=LocalCombatAttackType::Ranged)||
       !event.weaponPeriodMs||event.weaponPeriodMs>60000||
       !(swing||event.kind==LocalCombatEventKind::SpellDamage||event.kind==LocalCombatEventKind::ProcDamage))return 0;
    // PPM * base weapon milliseconds / 60000 is a probability; retain 1/100
    // percent precision and cap the final probability at one.
    return uint32_t(std::clamp(std::llround(double(p.ppm)*event.weaponPeriodMs/6.0),0LL,10000LL));
}
// Aura::CalcProcChance resolves the original aura caster at dispatch time.
// This timing is deliberately separate from the attacker's event metadata.
struct LocalProcCasterTiming {
    bool available=false;
    uint32_t weaponPeriodMs=0;
};
struct LocalProcChanceModifiers {
    float chanceFlat=0,chanceMultiplier=1,ppmFlat=0,ppmMultiplier=1;
};
inline uint32_t localProcChanceBasisPoints(const LocalProcDefinition& p,const LocalCombatEvent& event,
                                          LocalProcCasterTiming caster,LocalProcChanceModifiers modifiers={}) {
    if(!validLocalProcFilters(p))return 0;
    float chance=p.chance;
    if(caster.available) {
        if(!std::isfinite(modifiers.chanceFlat)||!std::isfinite(modifiers.chanceMultiplier)||
           !std::isfinite(modifiers.ppmFlat)||!std::isfinite(modifiers.ppmMultiplier))return 0;
        const bool swing=event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee||
            event.kind==LocalCombatEventKind::PetMelee||event.kind==LocalCombatEventKind::PlayerRanged;
        const bool damage=swing||event.kind==LocalCombatEventKind::SpellDamage||
            event.kind==LocalCombatEventKind::PeriodicDamage||event.kind==LocalCombatEventKind::ProcDamage;
        const bool heal=event.kind==LocalCombatEventKind::DirectHeal||event.kind==LocalCombatEventKind::PeriodicHeal||event.kind==LocalCombatEventKind::ProcHeal;
        const bool spellHit=event.kind==LocalCombatEventKind::SpellHit;
        if(p.ppm&&(damage||heal||spellHit)) {
            const auto type=event.attackType==LocalCombatAttackType::Unspecified?
                (event.kind==LocalCombatEventKind::PlayerRanged?LocalCombatAttackType::Ranged:swing?LocalCombatAttackType::Melee:LocalCombatAttackType::Magic):event.attackType;
            uint32_t period=0;
            if(swing||type==LocalCombatAttackType::Melee||type==LocalCombatAttackType::Ranged) {
                period=caster.weaponPeriodMs;
                if(!period||period>60000)return 0;
            } else {
                if(event.sourceRawCastTimeMs>600000)return 0;
                period=std::max(1500u,event.sourceRawCastTimeMs);
            }
            const float ppm=p.ppm*modifiers.ppmMultiplier+modifiers.ppmFlat;
            chance=float(period)*ppm/600.0f;
        }
        chance=chance*modifiers.chanceMultiplier+modifiers.chanceFlat;
    }
    return localProcReducedChanceBasisPoints(p,event,chance*100.0);
}
inline bool localRollProcBasisPoints(uint32_t chance,uint64_t& state) {
    if(chance>=10000)return true;
    if(!chance)return false;
    if(!state)state=0x9e3779b97f4a7c15ULL;
    state^=state>>12;state^=state<<25;state^=state>>27;
    return ((state*2685821657736338717ULL)>>32)%10000<chance;
}
inline bool localHasTimedAura(const LocalSpellDefinition& d) {
    return !d.passive&&(d.buffHealth || d.buffArmor || d.buffAbsorb || d.proc.effect!=LocalProcEffect::None ||
        d.periodicHealMaxHealthPct || d.physicalDamageDonePct || d.damageTakenPct || d.arcaneBlastProfile==2);
}
inline bool validLocalProcDefinition(const LocalSpellDefinition& d,const LocalProcDefinition& p) {
    if(p.spellFamily>1000||!validLocalProcFilters(p)||d.sourceDamageClass>3||d.mageArmorGroup>1)return false;
    // A recipient other than the aura owner is admitted only for the effect
    // that actually names one. Owner and recipient stay distinct concepts.
    if(p.recipient>uint8_t(LocalProcRecipient::OwnedPet))return false;
    if((p.recipient!=uint8_t(LocalProcRecipient::AuraOwner))!=(p.effect==LocalProcEffect::RestorePetPower))return false;
    // Owned-creature energize: a learned passive talent, no saved aura of its
    // own, one source power system and a bounded source amount. Its runtime
    // requires an actual live summon; a missing recipient grants nothing.
    if(p.effect==LocalProcEffect::RestorePetPower)
        return d.passive && d.talentId && !d.durationMs && !d.triggeredOnly && d.buffSelfOnly &&
            p.resourceType==2 && p.spellId && p.spellId!=d.id && p.flags &&
            p.chance && p.chance<=100 && !p.ppm && !p.charges && !p.cooldownMs &&
            p.amount && p.amount<=1000 && !p.amountPerLevel && !p.baseLevel && !p.maxLevel &&
            !p.range && !p.pushbackPercent && !p.requiredForms && !p.allowTriggered &&
            p.phaseMask==LocalProcPhaseHit && p.schoolMask && p.schoolMask<=127 &&
            !p.spellFamily && p.spellFamilyFlags==std::array<uint32_t,3>{} &&
            d.maxAuraStacks==1 && !d.buffHealth && !d.buffArmor && !d.buffAbsorb && !d.damage &&
            !d.heal && !d.periodicDamage && !d.periodicHeal && !d.manaPer5 && !d.manaPerAbsorbMilli &&
            !d.triggeredAuraSpellId && !d.meleeSpecialProfile && !d.procParentTalentId &&
            !d.stormstrikeProfile && !d.arcaneBlastProfile && !d.clearcastingProfile &&
            !d.physicalDamageDonePct && !d.damageTakenPct && !d.periodicHealMaxHealthPct;
    if(d.stormstrikeProfile) {
        if(d.stormstrikeProfile>5||!d.clientSpell||d.allowableClasses!=64||d.schoolMask!=(d.stormstrikeProfile==5?8u:1u)||
           d.clearcastingProfile||d.arcaneBlastProfile||d.meleeSpecialProfile||d.meleeHastePct||
           d.chargedCostPct||d.buffHealth||d.buffArmor||d.buffAbsorb||d.manaPer5||d.manaPerAbsorbMilli||
           d.damage||d.heal||d.periodicDamage||d.periodicHeal||d.periodicHealMaxHealthPct||
           d.physicalDamageDonePct||d.damageTakenPct||d.maxAuraStacks!=1)return false;
        if(d.stormstrikeProfile==4) {
            const auto rank=d.talentRank;
            return rank>=1&&rank<=2&&d.id==51520u+rank&&d.talentId==2054&&d.passive&&
                !d.triggeredOnly&&d.buffSelfOnly&&!d.durationMs&&!d.procParentTalentId&&
                d.stormstrikeManaChancePct==rank*50&&p.effect==LocalProcEffect::RestoreMana&&
                p.spellId==63375&&p.amount==20&&p.flags==16&&p.chance==rank*50&&!p.charges&&
                !p.cooldownMs&&!p.ppm&&!p.allowTriggered&&!p.amountPerLevel&&!p.baseLevel&&
                !p.maxLevel&&!p.range&&!p.pushbackPercent&&!p.resourceType&&!p.requiredForms&&
                p.schoolMask==8&&!p.spellFamily&&p.spellFamilyFlags==std::array<uint32_t,3>{}&&
                !p.triggerSchoolMask&&p.triggerSpellFamily==11&&
                p.triggerSpellFamilyFlags==std::array<uint32_t,3>{0,16777216,0}&&
                p.phaseMask==LocalProcPhaseCast&&p.spellTypeMask==7&&p.hitMask==LocalProcSupportedHits;
        }
        const LocalSpellDefinition empty;
        if(d.stormstrikeManaChancePct||d.passive||p.effect!=LocalProcEffect::None||
           !validLocalProcDefinition(empty,p))return false;
        if(d.stormstrikeProfile==1)return d.id==17364&&!d.triggeredOnly&&d.talentId==901&&
            d.talentRank==1&&!d.buffSelfOnly&&d.durationMs==12000&&d.cooldownMs==8000&&
            d.manaPercent==8&&!d.mana&&!d.resourceType&&d.range==5&&d.sourceDamageClass==2&&
            d.spellFamily==11&&d.spellFamilyFlags==std::array<uint32_t,3>{0,16777232,0}&&
            d.requiredItemClass==2&&d.requiredItemSubclasses==173555&&!d.weaponDamage;
        if(d.stormstrikeProfile==5)return d.id==63375&&d.triggeredOnly&&d.buffSelfOnly&&
            !d.durationMs&&!d.weaponDamage&&!d.procParentTalentId&&d.range==100;
        return d.id==(d.stormstrikeProfile==2?32175u:32176u)&&d.triggeredOnly&&
            d.procParentTalentId==901&&!d.durationMs&&d.weaponDamage&&d.weaponPercent==100&&
            !d.normalizedWeapon&&d.sourceDamageClass==2&&d.spellFamily==11&&
            d.spellFamilyFlags==std::array<uint32_t,3>{0,16,5120}&&d.range==5&&
            d.requiredItemClass==2&&d.requiredItemSubclasses==173555&&
            d.requiresMainHand==(d.stormstrikeProfile==2)&&d.requiresOffHand==(d.stormstrikeProfile==3);
    }
    if(d.arcaneBlastProfile) {
        if(d.arcaneBlastProfile>2||!d.clientSpell||d.allowableClasses!=128||d.passive||d.spellFamily!=3||d.schoolMask!=64||
           d.clearcastingProfile||d.chargedCostPct||d.periodicHealMaxHealthPct||d.physicalDamageDonePct||d.damageTakenPct||
           d.meleeHastePct||d.procParentTalentId||d.manaPer5||d.buffHealth||d.buffArmor||d.buffAbsorb||d.manaPerAbsorbMilli||
           d.triggeredAuraSpellId||d.meleeSpecialProfile||p.effect!=LocalProcEffect::None)return false;
        if(d.arcaneBlastProfile==2&&(d.id!=36032||!d.triggeredOnly||d.durationMs!=6000||d.maxAuraStacks!=4||
           !d.buffSelfOnly||d.damage||d.heal||d.periodicDamage||d.periodicHeal))return false;
        if(d.arcaneBlastProfile==1&&(d.triggeredOnly||(d.id!=30451&&d.id!=42894&&d.id!=42896&&d.id!=42897)||
           d.durationMs||d.maxAuraStacks!=1||d.sourceDamageClass!=1||!d.damage||d.heal||d.periodicDamage||d.periodicHeal))return false;
    }
    if(d.physicalDamageDonePct||d.damageTakenPct) {
        if(d.id!=12292||d.physicalDamageDonePct!=20||d.damageTakenPct!=5||d.passive||d.triggeredOnly||
           d.allowableClasses!=1||d.talentId!=165||d.talentRank!=1||!d.buffSelfOnly||d.durationMs!=30000||
           d.maxAuraStacks!=1||d.buffHealth||d.buffArmor||d.buffAbsorb||d.damage||d.heal||d.periodicDamage||
           d.periodicHeal||d.periodicHealMaxHealthPct||d.manaPer5||d.manaPerAbsorbMilli||d.meleeHastePct||
           d.procParentTalentId||d.triggeredAuraSpellId||d.meleeSpecialProfile||p.effect!=LocalProcEffect::None)return false;
    }
    constexpr std::array<uint32_t,3> bloodParents{16487,16489,16492},bloodChildren{16488,16490,16491};
    const auto bloodParent=std::find(bloodParents.begin(),bloodParents.end(),d.id);
    if(d.periodicHealMaxHealthPct||bloodParent!=bloodParents.end()) {
        const bool parent=bloodParent!=bloodParents.end();
        const auto child=std::find(bloodChildren.begin(),bloodChildren.end(),d.id);
        const unsigned rank=parent?unsigned(bloodParent-bloodParents.begin())+1:unsigned(child-bloodChildren.begin())+1;
        if(rank>3||d.allowableClasses!=1||!d.buffSelfOnly||d.maxAuraStacks!=1||d.buffHealth||d.buffArmor||
           d.buffAbsorb||d.damage||d.heal||d.periodicDamage||d.periodicHeal||d.manaPer5||d.manaPerAbsorbMilli||
           d.meleeHastePct||d.triggeredAuraSpellId||d.meleeSpecialProfile||d.clearcastingProfile||d.chargedCostPct)return false;
        if(parent)return d.passive&&!d.triggeredOnly&&!d.durationMs&&!d.periodicHealMaxHealthPct&&
            !d.periodicIntervalMs&&!d.procParentTalentId&&d.talentId==661&&d.talentRank==rank&&
            p.effect==LocalProcEffect::ApplyOwnerAura&&p.spellId==bloodChildren[rank-1]&&p.amount==1&&
            p.flags==664232&&p.chance==100&&!p.charges&&!p.cooldownMs&&!p.ppm&&!p.allowTriggered&&
            p.schoolMask==1&&!p.spellFamily&&p.spellFamilyFlags==std::array<uint32_t,3>{}&&
            p.spellTypeMask==1&&p.hitMask==LocalProcHitCritical&&p.phaseMask==LocalProcPhaseHit&&
            !p.amountPerLevel&&!p.baseLevel&&!p.maxLevel&&!p.range&&!p.pushbackPercent&&!p.resourceType&&
            !p.requiredForms&&!p.triggerSchoolMask&&!p.triggerSpellFamily&&p.triggerSpellFamilyFlags==std::array<uint32_t,3>{};
        const LocalSpellDefinition empty;
        return !d.passive&&d.triggeredOnly&&d.durationMs==6000&&d.periodicHealMaxHealthPct==1&&
            d.periodicIntervalMs==3000/rank&&d.procParentTalentId==661&&d.schoolMask==1&&!d.spellFamily&&
            d.spellFamilyFlags==std::array<uint32_t,3>{}&&p.effect==LocalProcEffect::None&&validLocalProcDefinition(empty,p);
    }
    if(d.spiritCritRatingPct||d.incomingCritReductionPct||d.procCanCrit) {
        const uint32_t child=d.id==30482?34913:d.id==43045?43043:d.id==43046?43044:0;
        const uint32_t amount=d.id==30482?75:d.id==43045?130:d.id==43046?170:0;
        if(!child||d.spiritCritRatingPct!=35||d.incomingCritReductionPct!=5||d.mageArmorGroup!=1||
           !d.procCanCrit||d.sourceDamageClass!=1||d.sourceCantCrit||d.triggeredOnly||d.passive||
           !d.buffSelfOnly||d.durationMs!=1800000||d.allowableClasses!=128||
           p.effect!=LocalProcEffect::DamageAttacker||p.spellId!=child||p.amount!=amount||
           p.flags!=139944||p.chance!=100||p.charges||p.cooldownMs||p.ppm||!p.allowTriggered||
           p.hitMask!=(LocalProcHitNormal|LocalProcHitCritical|LocalProcHitAbsorb)||
           p.spellTypeMask!=1||p.phaseMask!=LocalProcPhaseHit||p.schoolMask!=4||
           p.spellFamily!=3||p.spellFamilyFlags!=std::array<uint32_t,3>{0,8,0}||
           p.amountPerLevel||p.pushbackPercent||p.resourceType||p.requiredForms||
           p.triggerSchoolMask||p.triggerSpellFamily||
           p.triggerSpellFamilyFlags!=std::array<uint32_t,3>{})return false;
    }
    if(d.clearcastingProfile || d.chargedCostPct || (d.chargedCostMask[0]|d.chargedCostMask[1]|d.chargedCostMask[2]) ||
       p.effect==LocalProcEffect::ConsumeSpellCostCharge) {
        const bool parent=d.clearcastingProfile==1||d.clearcastingProfile==2;
        const bool druid=d.clearcastingProfile==2||d.clearcastingProfile==4;
        const uint32_t childId=druid?16870:12536,family=druid?7:3,talent=druid?827:75;
        constexpr std::array<uint32_t,5> mageRanks{11213,12574,12575,12576,12577};
        const std::array<uint32_t,3> flags{0,druid?2097152u:2u,druid?0u:8u};
        const std::array<uint32_t,3> masks=druid?std::array<uint32_t,3>{14924799u,126879699u,263168u}:
            std::array<uint32_t,3>{549591799u,168000u,0};
        if(d.clearcastingProfile<1||d.clearcastingProfile>4 || d.allowableClasses!=(druid?1024u:128u) ||
           p.spellId!=childId || p.amount!=1 || p.schoolMask!=(druid?8u:64u) || p.spellFamily!=family ||
           p.spellFamilyFlags!=flags || p.cooldownMs || p.amountPerLevel || p.baseLevel || p.maxLevel || p.range ||
           p.pushbackPercent || p.resourceType || p.requiredForms || p.triggerSchoolMask ||
           p.hitMask!=(LocalProcHitNormal|LocalProcHitCritical|LocalProcHitAbsorb) ||
           d.meleeHastePct || d.manaPer5 || d.manaPerAbsorbMilli || d.buffHealth || d.buffArmor || d.buffAbsorb ||
           d.damage || d.heal || d.periodicDamage || d.periodicHeal || d.triggeredAuraSpellId || d.meleeSpecialProfile ||
           d.maxAuraStacks!=1 || !d.buffSelfOnly)return false;
        if(parent)return d.passive && !d.triggeredOnly && !d.durationMs && !d.procParentTalentId &&
            !d.chargedCostPct && d.chargedCostMask==std::array<uint32_t,3>{} &&
            d.talentId==talent && (druid?(d.id==16864&&d.talentRank==1):
                (d.talentRank>=1&&d.talentRank<=5&&d.id==mageRanks[d.talentRank-1])) &&
            p.effect==LocalProcEffect::ApplyOwnerAura && p.flags==(druid?81924u:87376u) &&
            p.chance==(druid?100:2*d.talentRank) && p.ppm==(druid?3.5f:0.f) && !p.charges &&
            p.allowTriggered && p.spellTypeMask==(druid?7:1) && p.phaseMask==LocalProcPhaseHit &&
            p.triggerSpellFamily==(druid?0u:3u) && p.triggerSpellFamilyFlags==std::array<uint32_t,3>{};
        return d.id==childId && !d.passive && d.triggeredOnly && d.durationMs==15000 &&
            d.procParentTalentId==talent && d.chargedCostPct==(druid?-100:-1000) && d.chargedCostMask==masks &&
            d.spellFamily==family && d.spellFamilyFlags==flags && d.schoolMask==(druid?8u:64u) &&
            p.effect==LocalProcEffect::ConsumeSpellCostCharge && p.flags==87376 && p.chance==100 && p.charges==1 &&
            p.attributesMask==(LocalProcRequireManaCost|LocalProcRequireSpellMod) && p.sourceEffectMask==1 && !p.disableEffectsMask &&
            !p.ppm && !p.allowTriggered && p.phaseMask==LocalProcPhaseCast && p.spellTypeMask==7 &&
            p.triggerSpellFamily==family && p.triggerSpellFamilyFlags==masks;
    }
    if(d.meleeHastePct||d.procParentTalentId) {
        if(p.effect!=LocalProcEffect::ConsumeOwnerAuraCharge || !d.meleeHastePct || d.meleeHastePct>100 || !d.procParentTalentId)return false;
    }
    if(p.effect==LocalProcEffect::ApplyOwnerAura||p.effect==LocalProcEffect::ConsumeOwnerAuraCharge) {
        const bool parent=p.effect==LocalProcEffect::ApplyOwnerAura;
        return p.spellId && p.amount && p.amount<=100 && p.chance==100 && !p.ppm &&
            p.allowTriggered==parent && !p.pushbackPercent && !p.resourceType && !p.requiredForms &&
            !p.amountPerLevel && !p.range && !p.baseLevel && !p.maxLevel &&
            !p.triggerSchoolMask && !p.triggerSpellFamily &&
            !(p.triggerSpellFamilyFlags[0]|p.triggerSpellFamilyFlags[1]|p.triggerSpellFamilyFlags[2]) &&
            p.phaseMask==LocalProcPhaseHit && d.maxAuraStacks==1 &&
            !d.buffHealth && !d.buffArmor && !d.buffAbsorb && !d.damage && !d.heal &&
            !d.periodicDamage && !d.periodicHeal && !d.manaPer5 && !d.manaPerAbsorbMilli &&
            !d.triggeredAuraSpellId && !d.meleeSpecialProfile &&
            (parent ? (d.passive && !d.triggeredOnly && !d.durationMs && !d.meleeHastePct && !d.procParentTalentId &&
                !p.charges && !p.cooldownMs && p.flags==0x14 && p.hitMask==LocalProcHitCritical && p.spellTypeMask==1) :
                (!d.passive && d.triggeredOnly && d.durationMs==15000 && d.meleeHastePct==p.amount && d.procParentTalentId &&
                p.spellId==d.id && p.charges==3 && (p.cooldownMs==0||p.cooldownMs==500) && p.flags==4 &&
                p.hitMask==(LocalProcHitNormal|LocalProcHitCritical|LocalProcHitAbsorb) && p.spellTypeMask==7));
    }
    if(d.manaPerAbsorbMilli && (d.manaPerAbsorbMilli<1000 || d.manaPerAbsorbMilli>10000 ||
       !d.buffAbsorb || !d.buffSelfOnly || d.maxAuraStacks!=1 || !d.durationMs ||
       d.durationMs>3600000 || d.absorbSchoolMask!=127 || d.passive ||
       d.buffHealth || d.buffArmor || d.damage || d.heal || d.periodicDamage || d.periodicHeal))return false;
    if(p.effect==LocalProcEffect::Ignite)return d.passive&&d.talentId==34&&d.talentRank>=1&&d.talentRank<=5&&d.allowableClasses==128&&!d.durationMs&&p.amount==8u*d.talentRank&&validLocalIgniteProc(p);
    if(p.effect==LocalProcEffect::None)return !p.spellId&&!p.flags&&!p.charges&&!p.chance&&!p.cooldownMs&&!d.manaPer5&&
        !p.amount&&!p.baseLevel&&!p.maxLevel&&!p.schoolMask&&!p.spellFamily&&
        !(p.spellFamilyFlags[0]|p.spellFamilyFlags[1]|p.spellFamilyFlags[2])&&!p.amountPerLevel&&!p.range&&
        !p.triggerSchoolMask&&!p.triggerSpellFamily&&!(p.triggerSpellFamilyFlags[0]|p.triggerSpellFamilyFlags[1]|p.triggerSpellFamilyFlags[2])&&
        p.hitMask==(LocalProcHitNormal|LocalProcHitCritical)&&p.phaseMask==LocalProcPhaseHit&&p.spellTypeMask==7&&
        !p.ppm&&!p.allowTriggered&&!p.pushbackPercent&&!p.resourceType&&!p.requiredForms&&
        !p.attributesMask&&!p.disableEffectsMask&&p.sourceEffectMask==1;
    if(p.effect==LocalProcEffect::MeleeDamageShield)
        return p.spellId==d.id && p.flags==0x8 && p.chance==100 && !p.charges && !p.cooldownMs &&
            p.amount && p.amount<=100000 && p.amountPerLevel==0 && p.range==4 &&
            d.durationMs && d.durationMs<=3600000 && d.maxAuraStacks==1 && !d.buffSelfOnly &&
            !d.manaPer5 && !d.manaPerAbsorbMilli && !d.passive && !d.buffHealth && !d.buffArmor &&
            !d.buffAbsorb && !d.damage && !d.heal && !d.periodicDamage && !d.periodicHeal && !p.ppm&&!p.pushbackPercent&&!p.resourceType&&!p.requiredForms;
    // The runtime executes only these bounded leaf actions. Metadata may use
    // any supported event filter or PPM; the client importer separately admits
    // only source-reviewed profiles and rejects unimplemented secondary effects.
    if(p.effect==LocalProcEffect::HealOwnerPctMaxHealth)
        return d.triggeredOnly && !d.passive && d.durationMs && d.durationMs<=600000 && d.maxAuraStacks==1 &&
            p.spellId && p.flags && p.chance==100 && p.charges && !p.cooldownMs &&
            p.amount && p.amount<=100 && !p.amountPerLevel && !p.ppm && !p.pushbackPercent && !p.resourceType &&
            !p.requiredForms && !p.allowTriggered && p.phaseMask==LocalProcPhaseHit &&
            !d.buffHealth && !d.buffArmor && !d.buffAbsorb && !d.damage && !d.heal &&
            !d.periodicDamage && !d.periodicHeal && !d.manaPer5 && !d.manaPerAbsorbMilli &&
            !d.triggeredAuraSpellId && !d.meleeSpecialProfile;
    const bool passivePower=(p.effect==LocalProcEffect::RestorePower||p.effect==LocalProcEffect::AddComboPoints)&&d.passive;
    return (p.effect==LocalProcEffect::DamageAttacker || p.effect==LocalProcEffect::RestoreMana ||
            p.effect==LocalProcEffect::HealOwner || p.effect==LocalProcEffect::RestorePower || p.effect==LocalProcEffect::AddComboPoints) &&
        p.spellId && p.flags && (p.chance||p.ppm>0) &&
        p.cooldownMs<=60000 && p.amount && p.amount<=100000 && p.schoolMask<=127 &&
        (p.effect!=LocalProcEffect::DamageAttacker||p.schoolMask) && p.pushbackPercent<=100 &&
        (!p.pushbackPercent||p.effect==LocalProcEffect::HealOwner) &&
        (p.effect==LocalProcEffect::RestorePower?(p.resourceType==1||p.resourceType==3):!p.resourceType) &&
        (p.effect!=LocalProcEffect::AddComboPoints||(d.passive&&p.amount<=5&&!p.amountPerLevel)) &&
        (!p.requiredForms||d.passive) &&
        std::isfinite(p.amountPerLevel) && p.amountPerLevel>=0 && p.amountPerLevel<=10000 &&
        std::isfinite(p.range) && p.range>=0 && p.range<=100 && d.manaPer5<=100000 &&
        (passivePower?(d.talentId&&!d.durationMs&&!p.charges&&!p.cooldownMs&&!d.manaPer5&&p.amount<=100):
            (!d.passive&&d.durationMs&&d.durationMs<=3600000)) &&
        ((p.attributesMask&LocalProcUseStacksForCharges)?(!d.passive&&d.maxAuraStacks>0):d.maxAuraStacks==1) &&
        !d.buffHealth && !d.buffArmor && !d.buffAbsorb && !d.damage && !d.heal &&
        !d.periodicDamage && !d.periodicHeal && !d.manaPerAbsorbMilli;
}
inline bool validLocalProc(const LocalSpellDefinition& d) {
    if(!validLocalProcDefinition(d,d.proc))return false;
    if(d.secondaryProc.effect==LocalProcEffect::None) {
        // Empty metadata must retain every descriptor default; use an empty
        // parent because unrelated mana/shield payload belongs to primary only.
        const LocalSpellDefinition empty;
        return validLocalProcDefinition(empty,d.secondaryProc);
    }
    // A dual-aura profile is admitted only with two complete, disjoint passive
    // form branches. Neither branch can become an invisible timed/saved aura.
    return d.passive && d.proc.effect==LocalProcEffect::RestorePower &&
        d.secondaryProc.effect==LocalProcEffect::AddComboPoints &&
        d.proc.requiredForms && d.secondaryProc.requiredForms &&
        !(d.proc.requiredForms&d.secondaryProc.requiredForms) &&
        validLocalProcDefinition(d,d.secondaryProc);
}
// The same SpellEffectInfo::CalcValue (SpellInfo.cpp:414-431) the direct and
// periodic amounts use, over the proc record's own level columns. The record
// carries one level field, read from Spell.dbc column 39 (SpellLevel): over the
// 28 accepted definitions whose proc carries a non-zero one, columns 38 and 39
// agree on every row, so that single field already *is* the reference's
// max(BaseLevel, SpellLevel) subtrahend and a second column here would be a
// definition field with no producer (the implementation, measured).
inline uint32_t localProcAmount(const LocalRealmPlayer& owner,const LocalProcDefinition& d) {
    double amount=double(d.amount);
    if(d.amountPerLevel!=0.f) {
        int64_t level=int64_t(owner.level);
        if(level>int64_t(d.maxLevel)&&d.maxLevel>0)level=int64_t(d.maxLevel);
        else if(level<int64_t(d.baseLevel))level=int64_t(d.baseLevel);
        level-=int64_t(d.baseLevel);
        amount+=double(level)*double(d.amountPerLevel);
    }
    return std::isfinite(amount)?uint32_t(std::clamp(amount,0.0,1000000.0)):0;
}
// Rolls live on the authority. The deterministic PRNG has no guest-supplied roll.
inline bool localRollProc(uint8_t chance,uint64_t& state) {
    if(chance>=100)return true;
    if(!chance)return false;
    if(!state)state=0x9e3779b97f4a7c15ULL;
    state^=state>>12;state^=state<<25;state^=state>>27;
    return ((state*2685821657736338717ULL)>>32)%100<chance;
}
}
