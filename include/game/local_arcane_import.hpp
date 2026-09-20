#pragma once
#include "game/local_gameplay.hpp"
#include <initializer_list>
#include <utility>
namespace wowee::game {
template<class Tables>
inline bool localArcaneSourceMatches(const Tables& t,uint32_t row,uint32_t id,
        std::initializer_list<std::pair<uint32_t,uint32_t>> populated) {
    if(!t.spells||t.spells->getFieldCount()!=234||t.spells->getUInt32(row,0)!=id)return false;
    for(uint32_t col=1;col<234;++col) {
        if(col>=131&&col<=203)continue;
        uint32_t expected=0;for(const auto& [key,value]:populated)if(key==col){expected=value;break;}
        if(t.spells->getUInt32(row,col)!=expected)return false;
    }
    return true;
}
template<class Tables>
inline bool localArcaneTablesMatch(const Tables& t,bool blast) {
    const auto cast=Tables::lookup(t.castIndex,blast?19:1),range=Tables::lookup(t.rangeIndex,blast?4:1);
    if(cast<0||range<0||t.casts->getInt32(cast,1)!=(blast?2500:0)||t.casts->getInt32(cast,2)||
       t.casts->getInt32(cast,3)!=(blast?2500:0)||t.ranges->getFloat(range,1)||t.ranges->getFloat(range,2)||
       t.ranges->getFloat(range,3)!=(blast?30:0)||t.ranges->getFloat(range,4)!=(blast?30:0)||t.ranges->getUInt32(range,5))return false;
    return true;
}
template<class Tables>
inline bool decodeClientArcaneBlast(const Tables& t,uint32_t row,LocalSpellDefinition& d,LocalSpellDefinition* internalAura=nullptr) {
    if(d.allowableClasses!=128||!d.clientSpell)return false;
    bool matched=false;
    switch(d.id) {
        case 30451:matched=localArcaneSourceMatches(t,row,d.id,{{4u,262144u},{19u,1u},{28u,19u},{31u,15u},{35u,101u},{37u,68u},{38u,64u},{39u,64u},{46u,4u},{68u,4294967295u},{71u,2u},{72u,64u},{74u,137u},{77u,1084227584u},{80u,841u},{86u,6u},{87u,1u},{117u,36032u},{125u,536870912u},{128u,536870912u},{204u,7u},{205u,133u},{206u,1500u},{208u,3u},{209u,536870912u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,64u},{229u,1060554932u}});break;
        case 42894:matched=localArcaneSourceMatches(t,row,d.id,{{4u,262144u},{19u,1u},{28u,19u},{31u,15u},{35u,101u},{37u,75u},{38u,71u},{39u,71u},{46u,4u},{68u,4294967295u},{71u,2u},{72u,64u},{74u,145u},{77u,1084856730u},{80u,896u},{86u,6u},{87u,1u},{117u,36032u},{125u,536870912u},{128u,536870912u},{204u,7u},{205u,133u},{206u,1500u},{208u,3u},{209u,536870912u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,64u},{229u,1060554932u}});break;
        case 42896:matched=localArcaneSourceMatches(t,row,d.id,{{4u,262144u},{19u,1u},{28u,19u},{31u,15u},{35u,101u},{37u,80u},{38u,76u},{39u,76u},{46u,4u},{68u,4294967295u},{71u,2u},{72u,64u},{74u,169u},{75u,1u},{77u,1086744166u},{80u,1046u},{81u,4294967295u},{86u,6u},{87u,1u},{117u,36032u},{125u,536870912u},{128u,536870912u},{204u,7u},{205u,133u},{206u,1500u},{208u,3u},{209u,536870912u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,64u},{229u,1060554932u}});break;
        case 42897:matched=localArcaneSourceMatches(t,row,d.id,{{4u,262144u},{19u,1u},{28u,19u},{31u,15u},{35u,101u},{37u,84u},{38u,80u},{39u,80u},{46u,4u},{68u,4294967295u},{71u,2u},{72u,64u},{74u,193u},{77u,1088421888u},{80u,1184u},{86u,6u},{87u,1u},{117u,36032u},{125u,536870912u},{128u,536870912u},{204u,7u},{205u,133u},{206u,1500u},{208u,3u},{209u,536870912u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,64u},{229u,1060554932u}});break;
        default:return false;
    }
    const auto child=Tables::lookup(t.spellIndex,36032),duration=Tables::lookup(t.durationIndex,32);
    if(!matched||child<0||duration<0||!localArcaneTablesMatch(t,true)||!localArcaneTablesMatch(t,false)||
       t.durations->getInt32(duration,1)!=6000||t.durations->getInt32(duration,2)||t.durations->getInt32(duration,3)!=6000||
       !localArcaneSourceMatches(t,uint32_t(child),36032,{{4u,67371008u},{5u,1024u},{6u,268435456u},{7u,196608u},{8u,8388737u},{28u,1u},{34u,65536u},{35u,100u},{36u,1u},{38u,1u},{39u,1u},{40u,32u},{46u,1u},{49u,4u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,14u},{81u,174u},{86u,1u},{87u,1u},{95u,79u},{96u,108u},{110u,64u},{111u,14u},{122u,536870912u},{125u,536870912u},{128u,536870912u},{208u,3u},{211u,12u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,64u}}))return false;
    d.arcaneBlastProfile=1;d.buffSelfOnly=false;d.mageArmorGroup=0;
    d.damage=uint32_t(t.spells->getInt32(row,80)+1);
    d.damageMax=d.damage+uint32_t(t.spells->getInt32(row,74)-1);
    d.damagePerLevel=t.spells->getFloat(row,77);d.mana=0;d.manaPercent=7;d.resourceType=0;
    // Column 38 is BaseLevel, column 39 SpellLevel (DBCStructure.h:1679-1680).
    // Each pinned Arcane Blast closure above fixes both columns to the same
    // value, so this read is unchanged in value by the implementation's column correction;
    // d.spellLevel was already set from column 39 by the generic decoder.
    d.baseLevel=t.spells->getUInt32(row,38);d.maxLevel=t.spells->getUInt32(row,37);
    d.castTimeMs=d.sourceRawCastTimeMs=2500;d.globalCooldownMs=1500;d.interruptFlags=15;
    d.spellFamily=3;d.spellFamilyFlags={536870912,0,0};d.schoolMask=64;d.sourceDamageClass=1;
    d.range=30;d.unsupportedReason.clear();
    if(internalAura) {
        LocalSpellDefinition aura;aura.id=36032;aura.name=t.spells->getString(uint32_t(child),136);
        aura.iconId=t.spells->getUInt32(uint32_t(child),133);aura.visualId=t.spells->getUInt32(uint32_t(child),131);
        aura.clientSpell=true;aura.triggeredOnly=true;aura.arcaneBlastProfile=2;aura.allowableClasses=128;
        aura.durationMs=6000;aura.maxAuraStacks=4;aura.buffSelfOnly=true;aura.resourceType=0;
        aura.spellFamily=3;aura.spellFamilyFlags={0,0,12};aura.schoolMask=64;aura.range=0;
        *internalAura=std::move(aura);
    }
    return true;
}
template<class Tables>
inline bool decodeClientArcaneStability(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    constexpr uint32_t ids[]={11237,12463,12464,16769,16770};
    if(d.talentId!=80||d.allowableClasses!=128||d.talentRank<1||d.talentRank>5||d.id!=ids[d.talentRank-1]||
       !localArcaneTablesMatch(t,false)||!localArcaneSourceMatches(t,row,d.id,
       {{4,464},{28,1},{35,101},{39,1},{46,1},{68,4294967295u},{71,6},{74,1},{80,20u*d.talentRank-1},
        {86,1},{95,108},{110,9},{122,536872960},{208,3},{216,1065353216},{217,1065353216},{218,1065353216},{225,1}}))return false;
    d.passive=true;d.passivePushbackPct=20*d.talentRank;d.pushbackSpellMask={536872960,0,0};
    d.spellFamily=3;d.unsupportedReason.clear();return true;
}
}
