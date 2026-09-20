#pragma once
#include "game/local_gameplay.hpp"
#include <initializer_list>
#include <utility>

namespace wowee::game {
// Narrow, complete 12340 profiles for the normal Enhancement path to Flurry.
// Cosmetic text, icons and visuals are excluded; every gameplay column must
// match. These profiles do not grant points or bypass tier/prerequisite gates.
template<class Tables>
inline bool localProgressionSourceMatches(const Tables& t,uint32_t row,
        std::initializer_list<std::pair<uint32_t,uint32_t>> populated) {
    for(uint32_t col=1;col<234;++col) {
        if(col>=131&&col<=203)continue;
        uint32_t expected=0;
        for(const auto& [key,value]:populated)if(key==col){expected=value;break;}
        if(t.spells->getUInt32(row,col)!=expected)return false;
    }
    const auto cast=Tables::lookup(t.castIndex,t.spells->getUInt32(row,28));
    const auto range=Tables::lookup(t.rangeIndex,t.spells->getUInt32(row,46));
    if(cast<0||range<0||t.casts->getInt32(cast,1)||t.casts->getInt32(cast,2)||
       t.casts->getInt32(cast,3)||t.ranges->getFloat(range,1)||
       t.ranges->getFloat(range,2)||t.ranges->getFloat(range,3)||t.ranges->getFloat(range,4)||
       t.ranges->getUInt32(range,5))return false;
    if(const auto id=t.spells->getUInt32(row,40)) {
        const auto duration=Tables::lookup(t.durationIndex,id);
        if(duration<0||t.durations->getInt32(duration,1)!=-1||
           t.durations->getInt32(duration,2)||t.durations->getInt32(duration,3)!=-1)return false;
    }
    return true;
}
template<class Tables>
inline bool decodeClientProgressionTalent(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    if(!t.spells||t.spells->getFieldCount()!=234||t.spells->getUInt32(row,0)!=d.id||
       d.allowableClasses!=64||!d.talentRank||d.talentRank>5)return false;
    const uint32_t rank=d.talentRank;
    if(d.talentId==614) {
        if(d.id!=17484+rank||!localProgressionSourceMatches(t,row,
           {{4,464},{28,1},{35,101},{46,1},{68,0xffffffff},{71,6},{74,1},
            {80,rank*2-1},{86,1},{95,137},{110,3},{216,0x3f800000},
            {217,0x3f800000},{218,0x3f800000},{225,1},{229,0x3f800000},
            {230,0x3f800000},{231,0x3f800000}}))return false;
        d.passiveTotalStatPct={};d.passiveTotalStatPct[3]=uint8_t(rank*2);
    } else if(d.talentId==613) {
        constexpr uint32_t ids[]={16255,16302,16303,16304,16305};
        if(d.id!=ids[rank-1]||!localProgressionSourceMatches(t,row,
           {{4,464},{28,1},{35,101},{39,1},{46,1},{68,0xffffffff},
            {71,6},{72,6},{74,1},{75,1},{80,rank-1},{81,rank-1},{86,1},{87,1},
            {95,52},{96,57},{216,0x3f800000},{217,0x3f800000},
            {218,0x3f800000},{225,1}}))return false;
        d.passiveMeleeCritPct=uint8_t(rank);d.passiveSpellCritPct=uint8_t(rank);
        d.requiredItemClass=-1;d.requiredItemSubclasses=0;d.requiredInventoryTypes=0;
    } else if(d.talentId==605) {
        if(rank>2||d.id!=(rank==1?16262u:16287u))return false;
        const bool valid=rank==1?localProgressionSourceMatches(t,row,
           {{4,464},{28,1},{35,101},{40,21},{46,1},{68,0xffffffff},{71,6},{74,1},
            {80,uint32_t(-1001)},{86,1},{95,107},{107,2048},{110,10},{122,2048},
            {208,11},{216,0x3f800000},{225,1},{229,0x3f800000},
            {230,0x3f800000},{231,0x3f800000}}):localProgressionSourceMatches(t,row,
           {{4,464},{28,1},{35,101},{46,1},{68,0xffffffff},{71,6},{72,6},{74,1},{75,1},
            {80,uint32_t(-2001)},{81,0xffffffff},{86,1},{87,1},{95,107},{96,108},
            {110,10},{111,14},{122,2048},{125,2048},{208,11},{211,8},
            {216,0x3f800000},{217,0x3f800000},{218,0x3f800000},{225,1},
            {229,0x3f800000},{231,0x3f800000}});
        if(!valid)return false;
        d.passiveCastModifiers={};auto& mod=d.passiveCastModifiers[0];
        mod.active=true;mod.operation=10;mod.percentage=false;
        mod.amount=-int32_t(rank*1000);mod.mask={2048,0,0};
        // Rank 2's second effect is the source zero-percent cost marker.
        // Its complete zero profile was checked above; it changes no cost.
    } else return false;
    d.passive=true;d.buffSelfOnly=true;d.unsupportedReason.clear();return true;
}
}
