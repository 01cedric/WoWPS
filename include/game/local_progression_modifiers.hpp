#pragma once
#include "game/local_talents.hpp"

namespace wowee::game {
// Aura 79 / physical school. Apply once to outgoing physical amounts; callers
// retain their own rounding, mitigation and periodic application snapshots.
// Reading the current rank keeps learn/reset and every Druid form coherent.
inline float localTalentPhysicalDamageMultiplier(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(p.dead||p.classId<1||p.classId>11||!validLocalTalents(p))return 1.f;
    float multiplier=1.f;
    for(auto [id,rank]:p.talents) {
        const auto* d=localTalentSpell(c,id,rank);
        if(!d||!d->passive||!d->unsupportedReason.empty()||!d->passivePhysicalDamagePct||d->requiredItemClass>=0||
           !(d->allowableClasses&(1u<<(p.classId-1)))||!localTalentPrerequisitesReady(p,c,*d))continue;
        multiplier*=1.f+float(d->passivePhysicalDamagePct)/100.f;
    }
    return multiplier;
}
// Item-restricted aura79 belongs to one usable weapon attack. It must never
// leak through the unrestricted physical-spell/periodic modifier above.
inline float localTalentWeaponDamageMultiplier(const LocalRealmPlayer& p,const LocalWorldContent& c,
        uint32_t itemClass,uint32_t subclass,uint32_t inventoryType) {
    if(p.dead||p.classId<1||p.classId>11||!validLocalTalents(p))return 1.f;
    float multiplier=1.f;
    for(auto [id,rank]:p.talents) {
        const auto* d=localTalentSpell(c,id,rank);
        if(!d||!d->passive||!d->unsupportedReason.empty()||!d->passivePhysicalDamagePct||d->requiredItemClass<0||
           !(d->allowableClasses&(1u<<(p.classId-1)))||!localTalentPrerequisitesReady(p,c,*d)||
           uint32_t(d->requiredItemClass)!=itemClass||
           (d->requiredItemSubclasses&&(subclass>=32||!(d->requiredItemSubclasses&(1u<<subclass))))||
           (d->requiredInventoryTypes&&(inventoryType>=32||!(d->requiredInventoryTypes&(1u<<inventoryType)))))continue;
        multiplier*=1.f+float(d->passivePhysicalDamagePct)/100.f;
    }
    return multiplier;
}

}
