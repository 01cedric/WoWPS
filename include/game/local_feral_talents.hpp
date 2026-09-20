#pragma once
#include "game/local_talents.hpp"
namespace wowee::game {
// Form bonuses derive from the current allocation; no persisted multiplier or
// helper aura survives rank replacement, reset, death or leaving a form.
inline bool localFeralTalentReady(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition* d) {
    return p.classId==11&&!p.dead&&validLocalTalents(p)&&d&&d->passive&&d->unsupportedReason.empty()&&
        (d->allowableClasses&1024u)&&localTalentPrerequisitesReady(p,c,*d);
}
inline bool localDruidFeralForm(const LocalRealmPlayer& p) {
    const auto* f=localActiveForm(p);return f&&f->clazz==11&&(f->form==1||f->form==5||f->form==8);
}
inline float localTalentEquipmentArmorMultiplier(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    float multiplier=1.f;
    for(auto [id,rank]:p.talents)if(const auto* d=localTalentSpell(c,id,rank);localFeralTalentReady(p,c,d))
        multiplier*=1.f+float(d->passiveEquipmentArmorPct)/100.f;
    return multiplier;
}
inline uint32_t localFeralCritPct(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(!localDruidFeralForm(p))return 0;
    uint32_t amount=0;
    for(auto [id,rank]:p.talents)if(const auto* d=localTalentSpell(c,id,rank);localFeralTalentReady(p,c,d))
        amount+=d->passiveFeralCritPct;
    return std::min(100u,amount);
}
inline uint32_t localFeralDodgePct(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(!localDruidFeralForm(p))return 0;
    uint32_t amount=0;
    for(auto [id,rank]:p.talents)if(const auto* d=localTalentSpell(c,id,rank);localFeralTalentReady(p,c,d))
        amount+=d->passiveFeralDodgePct;
    return std::min(100u,amount);
}
inline float localFormRunPercent(const LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t slowPercent=100) {
    const auto* f=localActiveForm(p);
    if(!f||f->clazz!=11||f->form!=1)return localFormRunPercent(p,slowPercent);
    uint32_t bonus=0;
    for(auto [id,rank]:p.talents)if(const auto* d=localTalentSpell(c,id,rank);localFeralTalentReady(p,c,d))
        bonus=std::max(bonus,uint32_t(d->passiveCatRunPct));
    // Aura31 uses the strongest positive movement increase, followed by slows.
    return (100.f+bonus)*std::min(slowPercent,100u)/100.f;
}
}
