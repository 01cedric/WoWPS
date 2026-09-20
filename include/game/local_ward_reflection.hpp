#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_spell_amount.hpp"
#include "game/local_ward_ids.hpp"
namespace wowee::game {
struct LocalWardSpellPowerItem {uint32_t id;int32_t amount;};
inline constexpr LocalWardSpellPowerItem localWardSpellPowerItems[]={
#include "game/local_ward_spell_power_items_generated.inc"
};
// Only owned, correctly equipped ordinary source items contribute. Other
// spellpower aura families stay unavailable until their complete admission.
inline int32_t localWardEquipmentSpellPower(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(p.dead)return 0;
    int64_t total=0;
    for(size_t slot=0;slot<p.equipment.size();++slot) {
        const auto id=p.equipment[slot];const auto* item=id?c.item(id):nullptr;
        if(!item||!localEquipmentFits(item->inventoryType,item->slot,slot))continue;
        uint64_t copies=0;for(const auto& stack:p.inventory)if(stack.itemId==id)copies+=stack.count;
        if(uint64_t(std::count(p.equipment.begin(),p.equipment.begin()+slot+1,id))>copies)continue;
        const auto it=std::lower_bound(std::begin(localWardSpellPowerItems),std::end(localWardSpellPowerItems),id,
            [](const auto& a,uint32_t b){return a.id<b;});
        if(it!=std::end(localWardSpellPowerItems)&&it->id==id)total+=it->amount;
    }
    return int32_t(std::clamp(total,int64_t(-1000000),int64_t(1000000)));
}
// Unit::CalculateLevelPenalty, Unit.cpp:3208-3223. Every level this reads is
// SpellLevel, not BaseLevel: the reference tests `spellProto->SpellLevel` at
// :3211, :3217, :3218 and :3220. previously the field named baseLevel here was
// Spell.dbc column 39, SpellLevel, so this transcription was accidentally right
// and reads the right field explicitly now; the ward ranks carry the same value
// in both columns, so no absorb moves (the implementation, measured).
inline float localWardLevelPenalty(uint32_t level,const LocalSpellDefinition& d) {
    if(!d.spellLevel||d.spellLevel>=d.maxLevel)return 1.f;
    if(!level)return 0.f;
    const float factor=std::min(1.f,float(d.spellLevel+6)/float(level));
    const float penalty=d.spellLevel<20?float(20-d.spellLevel)*3.75f:0.f;
    return factor*(1.f-penalty/100.f);
}
inline uint32_t localWardAbsorbAmount(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d) {
    if(!d.wardProfile||d.wardProfile!=localWardIdProfile(d.id))return d.buffAbsorb;
    const auto all=localSpellAmountModifier(p,c,d,float(d.buffAbsorb),8);
    const auto base=int32_t(localSpellAmountModifier(p,c,d,all,3));
    // Upstream computes float bonus first, then truncates it separately before
    // adding it to the already integral source effect amount.
    float bonus=0.8068f;bonus*=float(localWardEquipmentSpellPower(p,c));bonus*=localWardLevelPenalty(p.level,d);
    return uint32_t(std::clamp(int64_t(base)+int64_t(bonus),int64_t(0),int64_t(1000000)));
}
inline uint16_t localWardReflectChanceBasisPoints(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d) {
    if(!d.wardProfile||d.wardProfile!=localWardIdProfile(d.id))return 0;
    // Effect1 CalcValue is zero for every rank (8457 uses zero die sides).
    const auto all=localSpellAmountModifier(p,c,d,0.f,8);
    const auto chance=uint32_t(localSpellAmountModifier(p,c,d,all,12));
    return uint16_t(std::min(100u,chance)*100);
}
inline bool localSpellCanReflect(const LocalSpellDefinition& d,bool positive=false,bool alreadyReflected=false) {
    return d.clientSpell&&d.unsupportedReason.empty()&&d.sourceDamageClass==1&&!d.passive&&
        !d.sourceCantReflect&&!positive&&!alreadyReflected;
}
inline uint32_t localWardIncomingReflectChanceBasisPoints(const LocalRealmPlayer& target,const LocalWorldContent& c,
    const LocalSpellDefinition& incoming,uint64_t sourceGuid,bool alreadyReflected=false) {
    if(!sourceGuid||sourceGuid==target.guid||target.dead||!target.health||
       !localSpellCanReflect(incoming,false,alreadyReflected)||target.statAuras.size()>kLocalMaxStatAuras)return 0;
    uint32_t chance=0;
    for(const auto& aura:target.statAuras) {
        const auto* ward=c.spell(aura.spellId);
        if(!ward||!ward->unsupportedReason.empty()||ward->wardProfile!=localWardIdProfile(ward->id)||
           !ward->wardProfile||!aura.remainingMs||aura.remainingMs>ward->durationMs||
           !aura.absorbRemaining||aura.absorbRemaining>1000000||aura.stacks!=1||
           aura.mapId!=target.mapId||aura.instanceId!=target.instanceId||
           !(ward->absorbSchoolMask&incoming.schoolMask)||aura.reflectChanceBasisPointsSnapshot>10000)continue;
        chance=std::min(10000u,chance+aura.reflectChanceBasisPointsSnapshot);
    }
    return chance;
}
inline uint8_t localMoltenShieldsChancePct(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(p.dead||p.classId!=8||!validLocalTalents(p))return 0;
    for(const auto& [id,rank]:p.talents) {
        if(id!=24)continue;
        const auto* talent=localTalentSpell(c,id,rank);
        if(talent&&talent->passive&&talent->unsupportedReason.empty()&&talent->allowableClasses==128&&
           localTalentPrerequisitesReady(p,c,*talent))return talent->moltenShieldsChancePct;
    }
    return 0;
}
inline uint8_t localMoltenArmorEventChancePct(const LocalRealmPlayer& owner,const LocalWorldContent& c,
    const LocalCombatEvent& event) {
    if(event.target!=owner.guid)return 0;
    if(!event.spell||event.kind==LocalCombatEventKind::PlayerMelee||event.kind==LocalCombatEventKind::NpcMelee)return 100;
    return localMoltenShieldsChancePct(owner,c);
}
}
