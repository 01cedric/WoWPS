#pragma once
#include "game/local_proc_rules.hpp"
#include "game/local_talents.hpp"

namespace wowee::game {
// Player::ApplySpellMod operations 18 and 26 use additive percentage
// multipliers followed by a flat addition. The affected spell is the PROC
// AURA, not its triggered leaf or the attacking spell. The caller resolves
// that aura's original caster, including incoming procs on another player.
//
// Two modifier sources exist in the reference and both are admitted here:
//   * a learned passive talent, and
//   * a timed aura the caster is carrying. AuraEffect::ApplySpellMod imposes
//     neither a talent prerequisite nor a `passive` requirement on the latter,
//     so this pass must not either.
//
// Charges follow the reference's split rule (Player.cpp:10213): an aura whose
// spell also carries a proc definition is debited by the proc path, never by
// modifier consumption, so the same charge cannot be spent twice. A charged
// aura that has already spent its charges contributes nothing
// (Player::IsAffectedBySpellmod, Player.cpp:9997).
inline void localAccumulateProcChanceModifier(const LocalPassiveCastModifier& mod,
                                              const LocalSpellDefinition& aura,
                                              LocalProcChanceModifiers& result) {
    if(!mod.active||(mod.operation!=18&&mod.operation!=26))return;
    bool affected=false;
    for(unsigned word=0;word<3;++word)affected=affected||bool(mod.mask[word]&aura.spellFamilyFlags[word]);
    if(!affected)return;
    float& flat=mod.operation==18?result.chanceFlat:result.ppmFlat;
    float& multiplier=mod.operation==18?result.chanceMultiplier:result.ppmMultiplier;
    if(mod.percentage) {
        // Source skips further percentages once a -100% accumulated
        // multiplier reaches zero. Flat terms are still independent.
        if(multiplier!=0.f)multiplier+=float(mod.amount)/100.f;
    } else flat+=float(mod.amount);
}

/// Whether the proc path owns this aura's charge debit rather than modifier
/// consumption. Player.cpp:10213: RemoveSpellMods skips an aura whose spell has
/// a spell_proc entry, because ConsumeProcCharges debits it instead.
inline bool localModifierChargeOwnedByProc(const LocalSpellDefinition& d) {
    return d.proc.effect!=LocalProcEffect::None;
}

inline LocalProcChanceModifiers localProcChanceModifiersForCaster(
        const LocalRealmPlayer* caster,const LocalWorldContent& content,
        const LocalSpellDefinition& aura) {
    LocalProcChanceModifiers result;
    if(!caster||caster->dead||caster->classId<1||caster->classId>11||
       !aura.spellFamily||aura.sourceIgnoreCasterModifiers)return result;
    if(validLocalTalents(*caster))for(const auto& [id,rank]:caster->talents) {
        const auto* talent=localTalentSpell(content,id,rank);
        if(!talent||!talent->passive||!talent->unsupportedReason.empty()||
           !(talent->allowableClasses&(1u<<(caster->classId-1)))||
           talent->spellFamily!=aura.spellFamily||!localTalentPrerequisitesReady(*caster,content,*talent))continue;
        for(const auto& mod:talent->passiveCastModifiers)
            localAccumulateProcChanceModifier(mod,aura,result);
    }
    // A timed aura the caster carries is an equally real modifier source. It is
    // bounded by kLocalMaxStatAuras and must be the caster's own application in
    // the caster's own map and instance.
    for(const auto& applied:caster->statAuras) {
        if(!applied.remainingMs||applied.mapId!=caster->mapId||applied.instanceId!=caster->instanceId)continue;
        if(applied.casterGuid&&applied.casterGuid!=caster->guid)continue;
        const auto* d=content.spell(applied.spellId);
        if(!d||!d->unsupportedReason.empty()||d->spellFamily!=aura.spellFamily)continue;
        // A charged aura with nothing left to spend contributes nothing.
        if(d->proc.charges&&!applied.procCharges)continue;
        for(const auto& mod:d->passiveCastModifiers)
            localAccumulateProcChanceModifier(mod,aura,result);
    }
    return result;
}
}
