#pragma once
#include "game/local_gameplay.hpp"
#include <array>
#include <initializer_list>
#include <utility>

namespace wowee::game {
// Pinned build 12340 source closure, see the source audit.
// Exact gameplay records prevent partial admission of additional effects.
template<class Tables>
inline bool warriorProgressionSourceMatches(const Tables& t,uint32_t row,uint32_t id,
        std::initializer_list<std::pair<uint32_t,uint32_t>> populated) {
    if(!t.spells||t.spells->getFieldCount()!=234||t.spells->getUInt32(row,0)!=id)return false;
    for(uint32_t col=1;col<234;++col) {
        if(col>=131&&col<=203)continue;
        uint32_t expected=0;
        for(const auto& [key,value]:populated)if(key==col){expected=value;break;}
        if(t.spells->getUInt32(row,col)!=expected)return false;
    }
    return true;
}
template<class Tables>
inline bool warriorProgressionSelfTables(const Tables& t,uint32_t durationId=0,int32_t durationMs=0) {
    if(!t.casts||!t.ranges||!t.durations)return false;
    const auto cast=Tables::lookup(t.castIndex,1),range=Tables::lookup(t.rangeIndex,1);
    if(cast<0||range<0)return false;
    for(uint32_t col=1;col<=3;++col)if(t.casts->getUInt32(cast,col))return false;
    for(uint32_t col=1;col<=5;++col)if(t.ranges->getUInt32(range,col))return false;
    if(durationId) {
        const auto duration=Tables::lookup(t.durationIndex,durationId);
        if(duration<0||t.durations->getInt32(duration,1)!=durationMs||
           t.durations->getInt32(duration,2)||t.durations->getInt32(duration,3)!=durationMs)return false;
    }
    return true;
}
template<class Tables>
inline bool decodeClientWarriorProgressionTalent(const Tables& t,uint32_t row,LocalSpellDefinition& d,
                                                LocalSpellDefinition* internalAura=nullptr) {
    if(d.allowableClasses!=1||!d.talentRank)return false;
    const uint32_t rank=d.talentRank;
    if(d.talentId==2250) { // Armored to the Teeth: TOTAL armor /108, /54, /36.
        constexpr uint32_t ids[]={61216,61221,61222};
        if(rank>3||d.id!=ids[rank-1])return false;
        const uint32_t divisor=108/rank;
        if(!warriorProgressionSourceMatches(t,row,d.id,{{4,464},{9,512},{28,1},{35,101},{39,1},{46,1},
            {68,0xffffffff},{71,6},{72,3},{74,1},{75,1},{80,divisor-1},{81,rank-1},{86,1},
            {95,285},{110,1},{122,196608},{123,128},{125,196608},{126,128},{208,4},
            {216,0x3f800000},{217,0x3f800000},{218,0x3f800000},{225,1}})||
            !warriorProgressionSelfTables(t))return false;
        // Effect3 is the source dummy rank/tooltip marker, not another AP grant.
        d.passiveArmorAttackPowerDivisor=uint16_t(divisor);
    } else if(d.talentId==1581) { // Dual Wield Specialization: offhand only.
        if(rank>5||d.id!=23583+rank)return false;
        if(!warriorProgressionSourceMatches(t,row,d.id,{{4,464},{28,1},{35,101},{40,21},{46,1},
            {68,0xffffffff},{71,6},{74,1},{80,rank*5-1},{86,1},{95,122},{208,4},
            {216,0x3f800000},{225,1},{229,0x3f800000}})||
            !warriorProgressionSelfTables(t,21,-1))return false;
        d.passiveOffhandDamagePct=uint8_t(rank*5);
    } else if(d.talentId==1657) { // Precision: globally active while a fitting weapon is usable.
        if(rank>3||d.id!=29589+rank)return false;
        if(!warriorProgressionSourceMatches(t,row,d.id,{{4,448},{28,1},{35,101},{40,21},{46,1},
            {68,2},{69,173555},{71,6},{74,1},{80,rank-1},{86,1},{95,54},
            {216,0x3f800000},{225,1},{229,0x3f800000}})||
            !warriorProgressionSelfTables(t,21,-1))return false;
        d.passiveWeaponHitPct=uint8_t(rank);d.requiredItemClass=2;
        d.requiredItemSubclasses=173555;d.requiredInventoryTypes=0;
    } else if(d.talentId==661) { // Blood Craze: incoming damage critical -> one percent regeneration.
        constexpr uint32_t ids[]={16487,16489,16492},children[]={16488,16490,16491};
        if(rank>3||d.id!=ids[rank-1])return false;
        const uint32_t childId=children[rank-1],interval=3000/rank;
        if(!warriorProgressionSourceMatches(t,row,d.id,{{4,464},{28,1},{34,664232},{35,100},{46,1},
            {68,0xffffffff},{71,6},{86,1},{95,42},{116,childId},{216,0x3f800000},
            {217,0x3f800000},{218,0x3f800000},{225,1},{229,0x3f800000},{230,0x3f800000},{231,0x3f800000}})||
            !warriorProgressionSelfTables(t))return false;
        const auto child=Tables::lookup(t.spellIndex,childId);
        if(child<0||!warriorProgressionSourceMatches(t,uint32_t(child),childId,{
            {4,16777232},{8,524416},{9,8},{28,1},{35,101},{40,32},{46,1},
            {68,0xffffffff},{71,6},{74,1},{86,1},{95,20},{98,interval},
            {216,0x3f800000},{217,0x3f800000},{218,0x3f800000},{225,1},{230,0x3f800000},{231,0x3f800000}})||
            !warriorProgressionSelfTables(t,32,6000))return false;
        LocalSpellDefinition aura;aura.id=childId;aura.clientSpell=true;aura.triggeredOnly=true;
        aura.allowableClasses=1;aura.buffSelfOnly=true;aura.range=0;aura.schoolMask=1;
        aura.durationMs=6000;aura.periodicIntervalMs=interval;aura.periodicHealMaxHealthPct=1;
        aura.procParentTalentId=661;aura.baseLevel=1;
        aura.name=t.spells->getString(child,136);aura.iconId=t.spells->getUInt32(child,133);
        LocalProcDefinition proc;proc.effect=LocalProcEffect::ApplyOwnerAura;proc.spellId=childId;
        proc.flags=664232;proc.chance=100;proc.amount=1;proc.schoolMask=1;
        proc.hitMask=LocalProcHitCritical;proc.spellTypeMask=1;
        d.proc=proc;
        if(internalAura)*internalAura=std::move(aura);
    } else if(d.talentId==165) { // Death Wish: BOTH outgoing benefit and incoming penalty.
        if(rank!=1||d.id!=12292)return false;
        if(!warriorProgressionSourceMatches(t,row,d.id,{{2,9},{3,31},{4,134479888},{5,98304},{28,1},
            {29,180000},{35,101},{38,30},{39,30},{40,9},{41,1},{42,100},{46,1},{68,0xffffffff},
            {71,6},{73,6},{74,1},{75,1},{76,1},{80,19},{81,0xffffffff},{82,4},{86,1},{88,1},
            {95,79},{97,87},{110,1},{112,127},{205,133},{206,1500},{208,4},{209,1048576},
            {216,0x3f800000},{217,0x3f800000},{218,0x3f800000},{225,1}})||
            !warriorProgressionSelfTables(t,9,30000))return false;
        d.physicalDamageDonePct=20;d.damageTakenPct=5;d.durationMs=30000;
        d.resourceType=1;d.mana=10;d.cooldownMs=180000;d.globalCooldownMs=1500;
        d.schoolMask=1;d.spellFamily=4;d.spellFamilyFlags={1048576,0,0};
        d.baseLevel=30;d.castTimeMs=0;d.range=0;d.passive=false;
        d.buffSelfOnly=true;d.unsupportedReason.clear();return true;
    } else return false;
    d.passive=true;d.buffSelfOnly=true;d.unsupportedReason.clear();return true;
}
}
