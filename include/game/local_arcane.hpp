#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_aura_identity.hpp"
#include <algorithm>

namespace wowee::game {
inline const LocalSpellDefinition* localArcaneBlastAura(const LocalWorldContent& c) {
    const auto* d=c.spell(36032);
    return d&&d->arcaneBlastProfile==2&&d->triggeredOnly&&d->clientSpell&&
        d->unsupportedReason.empty()&&d->allowableClasses==128&&d->durationMs==6000&&
        d->maxAuraStacks==4?d:nullptr;
}
inline uint8_t localArcaneBlastStacks(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(p.dead||!p.health||p.classId!=8||!localArcaneBlastAura(c))return 0;
    for(const auto& a:p.statAuras)if(a.spellId==36032&&a.remainingMs&&a.remainingMs<=6000&&
        a.mapId==p.mapId&&a.instanceId==p.instanceId&&a.casterGuid==p.guid&&a.stacks>=1&&a.stacks<=4)
        return a.stacks;
    return 0;
}
inline int32_t localArcaneBlastCostPct(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                     const LocalSpellDefinition& d) {
    return d.clientSpell&&!d.passive&&!d.triggeredOnly&&d.unsupportedReason.empty()&&
        d.spellFamily==3&&(d.spellFamilyFlags[0]&536870912u)?175*localArcaneBlastStacks(p,c):0;
}
inline uint32_t localArcaneBlastDamage(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                     const LocalSpellDefinition& d,uint32_t amount) {
    if(!d.clientSpell||d.unsupportedReason.size()||!(d.schoolMask&64u))return amount;
    return uint32_t(std::min(uint64_t(1000000),uint64_t(amount)*(100+15*localArcaneBlastStacks(p,c))/100));
}
inline bool localArcaneBlastCanApply(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                    const LocalSpellDefinition& d) {
    if(d.arcaneBlastProfile!=1)return true;
    return p.classId==8&&localArcaneBlastAura(c)&&
        (p.statAuras.size()<kLocalMaxStatAuras||std::any_of(p.statAuras.begin(),p.statAuras.end(),
            [](const auto& a){return a.spellId==36032||!a.remainingMs;}));
}
// The script's AfterCast hook runs after this cast's damage, so a new stack
// never increases its own hit. Refreshes at four stacks still restore six seconds.
inline bool applyLocalArcaneBlast(LocalRealmPlayer& p,const LocalWorldContent& c,
                                  const LocalSpellDefinition& d) {
    if(d.arcaneBlastProfile!=1||!localArcaneBlastCanApply(p,c,d))return false;
    const uint8_t stacks=std::min<unsigned>(4,localArcaneBlastStacks(p,c)+1);
    auto found=std::find_if(p.statAuras.begin(),p.statAuras.end(),[](const auto& a){return a.spellId==36032;});
    if(found==p.statAuras.end())found=std::find_if(p.statAuras.begin(),p.statAuras.end(),[](const auto& a){return !a.remainingMs;});
    LocalStatAura aura{36032,6000,p.mapId,p.instanceId,p.guid};aura.stacks=stacks;
    localPrepareAuraApplication(p,aura,found==p.statAuras.end()?nullptr:&*found);
    if(found==p.statAuras.end())p.statAuras.push_back(aura);else *found=aura;
    return true;
}
// The pinned spell_proc row selects Arcane Explosion / Arcane Barrage hit
// events. Admission of Barrage remains separate. The caller verifies a normal
// or critical hostile magic hit, rather than consuming on cast/cancel/expiry.
inline bool consumeLocalArcaneBlast(LocalRealmPlayer& p,const LocalWorldContent& c,
                                    const LocalSpellDefinition& d) {
    if(!d.clientSpell||!d.unsupportedReason.empty()||d.spellFamily!=3||
        !((d.spellFamilyFlags[0]&4096u)||(d.spellFamilyFlags[1]&32768u))||!localArcaneBlastStacks(p,c))return false;
    const auto count=p.statAuras.size();
    std::erase_if(p.statAuras,[](const auto& a){return a.spellId==36032;});
    return p.statAuras.size()!=count;
}
}
