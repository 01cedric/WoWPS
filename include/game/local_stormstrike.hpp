#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_npc_auras.hpp"
#include "game/local_melee.hpp"
#include "game/local_spell_equipment.hpp"
#include "game/local_talents.hpp"
namespace wowee::game {
inline bool localStormstrikeParent(const LocalSpellDefinition& d) {return d.stormstrikeProfile==1&&d.id==17364;}
inline bool localStormstrikeTalentReady(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d) {
    if(!d.stormstrikeProfile)return true;
    return localStormstrikeParent(d)&&p.classId==7&&validLocalTalents(p)&&
        std::find(p.talents.begin(),p.talents.end(),std::make_pair(uint32_t(901),uint8_t(1)))!=p.talents.end()&&
        localTalentPrerequisitesReady(p,c,d);
}
inline bool localStormstrikeWeaponReady(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& child,bool offHand) {
    const auto* weapon=localMeleeItem(p.equipment[offHand?16:15]);
    return weapon&&weapon->itemClass==2&&weapon->subclass<32&&(173555u&(1u<<weapon->subclass))&&
        child.id==(offHand?32176u:32175u)&&child.stormstrikeProfile==(offHand?3:2)&&child.triggeredOnly&&
        child.unsupportedReason.empty()&&localSpellEquipmentReady(p,c,child)&&
        (!offHand||localMeleeStats(p,c).offHand);
}
inline size_t localStormstrikeSlot(const LocalRealmNpc& n,uint64_t caster) {
    for(size_t i=0;i<n.stormstrikeAuras.size();++i)if(n.stormstrikeAuras[i].casterGuid==caster||!n.stormstrikeAuras[i].remainingMs||!n.stormstrikeAuras[i].charges)return i;
    return n.stormstrikeAuras.size();
}
inline uint32_t localStormstrikeDamage(const LocalRealmNpc& n,const LocalRealmPlayer& caster,const LocalSpellDefinition* spell,uint32_t amount,uint32_t elapsedMs=0) {
    if(caster.dead||caster.flight.active||caster.transportEntry||caster.mapId!=n.mapId||caster.instanceId!=n.instanceId)return amount;
    if(!spell||spell->spellFamily!=11||!(spell->spellFamilyFlags[0]&0x00100403u||spell->spellFamilyFlags[1]&0x2000u))return amount;
    for(const auto& a:n.stormstrikeAuras)if(a.casterGuid==caster.guid&&a.casterRevision==caster.positionRevision&&a.remainingMs>elapsedMs&&a.charges)
        return uint32_t(std::min(uint64_t(1000000),uint64_t(amount)*120/100));
    return amount;
}
inline void consumeLocalStormstrikeCharge(LocalRealmNpc& n,const LocalRealmPlayer& caster,const LocalSpellDefinition* spell,uint32_t damage,LocalMeleeOutcome outcome,uint32_t triggeredByAura,bool periodic=false,uint32_t elapsedMs=0) {
    if(caster.dead||caster.flight.active||caster.transportEntry||caster.mapId!=n.mapId||caster.instanceId!=n.instanceId)return;
    if(!spell||!(spell->schoolMask&8)||!damage||periodic||triggeredByAura==17364||(outcome!=LocalMeleeOutcome::Hit&&outcome!=LocalMeleeOutcome::Critical))return;
    for(auto& a:n.stormstrikeAuras)if(a.casterGuid==caster.guid&&a.casterRevision==caster.positionRevision&&a.remainingMs>elapsedMs&&a.charges)--a.charges;
    std::erase_if(n.stormstrikeAuras,[](const auto& a){return !a.charges||!a.remainingMs;});
}
inline LocalMeleeOutcome localStormstrikeChildOutcome(const LocalRealmPlayer& p,const LocalRealmNpc& n,const LocalMeleeStats& s,bool offHand,uint32_t roll) {
    const float chance=std::clamp((offHand?s.offHandCrit:s.crit)-(int(n.level)-int(p.level))*.2f,0.f,100.f);
    return roll<chance*100?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit;
}
}
