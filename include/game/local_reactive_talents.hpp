#pragma once
#include "game/local_proc_talents.hpp"
#include "game/local_progression_modifiers.hpp"
namespace wowee::game {
inline bool localTimedDamageTalentReady(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d) {
    if(!d.physicalDamageDonePct&&!d.damageTakenPct)return true;
    if(p.classId!=1||!validLocalTalents(p)||!d.talentId||!validLocalProc(d)||!d.unsupportedReason.empty())return false;
    return std::any_of(p.talents.begin(),p.talents.end(),[&](const auto& t){
        return t.first==d.talentId&&t.second==d.talentRank&&localTalentPrerequisitesReady(p,c,d);
    });
}
inline float localTimedDamageMultiplier(const LocalRealmPlayer& p,const LocalWorldContent& c,bool taken) {
    if(p.dead||!p.health)return 1.f;
    float value=1.f;
    for(const auto& aura:p.statAuras) {
        const auto* d=c.spell(aura.spellId);
        if(!aura.remainingMs||aura.mapId!=p.mapId||aura.instanceId!=p.instanceId||aura.casterGuid!=p.guid||
           !d||aura.remainingMs>d->durationMs||!localTimedDamageTalentReady(p,c,*d))continue;
        const auto percent=taken?d->damageTakenPct:d->physicalDamageDonePct;
        value*=1.f+float(percent)/100.f;
    }
    return value;
}
inline uint32_t localReactiveDamageAmount(uint32_t amount,float multiplier) {
    return std::isfinite(multiplier)&&multiplier>0?
        uint32_t(std::clamp(float(amount)*multiplier,0.f,1000000.f)):0;
}
inline uint32_t localPhysicalDamageAfterTalents(const LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t amount) {
    return localReactiveDamageAmount(amount,localTalentPhysicalDamageMultiplier(p,c)*localTimedDamageMultiplier(p,c,false));
}
// Death Wish aura 87 has all-school mask 127. The caller supplies the actual
// damage producer's ordering before armor, block and absorption.
inline uint32_t localIncomingDamageAfterTalents(const LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t amount) {
    return localReactiveDamageAmount(amount,localTimedDamageMultiplier(p,c,true));
}
}
