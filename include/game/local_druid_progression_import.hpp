#pragma once
#include "game/local_progression_import.hpp"

namespace wowee::game {
template<class Tables>
inline bool localDruidProgressionSourceMatches(const Tables& t,uint32_t row,
        std::initializer_list<std::pair<uint32_t,uint32_t>> populated) {
    // The ordinary strict matcher also checks the referenced self range and
    // passive cast entry. Natural Shapeshifter ranks 2/3 are the one reviewed
    // exception: raw duration 32 is corrected to duration 21 by the server.
    if(t.spells->getUInt32(row,40)!=32)return localProgressionSourceMatches(t,row,populated);
    for(uint32_t col=1;col<234;++col) {
        if(col>=131&&col<=203)continue;
        uint32_t expected=0;
        for(const auto& [key,value]:populated)if(key==col){expected=value;break;}
        if(t.spells->getUInt32(row,col)!=expected)return false;
    }
    const auto cast=Tables::lookup(t.castIndex,t.spells->getUInt32(row,28));
    const auto range=Tables::lookup(t.rangeIndex,t.spells->getUInt32(row,46));
    if(cast<0||range<0)return false;
    for(uint32_t col=1;col<=3;++col)if(t.casts->getUInt32(cast,col))return false;
    for(uint32_t col=1;col<=5;++col)if(t.ranges->getUInt32(range,col))return false;
    for(const uint32_t id:{21u,32u}) {
        const auto duration=Tables::lookup(t.durationIndex,id);
        const int32_t amount=id==21?-1:6000;
        if(duration<0||t.durations->getInt32(duration,1)!=amount||
           t.durations->getInt32(duration,2)||t.durations->getInt32(duration,3)!=amount)return false;
    }
    return true;
}
// Full 12340 source profiles for the ordinary Restoration opening. The two
// effects of each talent are inseparable; no points are awarded by this decoder.
template<class Tables>
inline bool decodeClientDruidProgressionTalent(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    if(!t.spells||t.spells->getFieldCount()!=234||t.spells->getUInt32(row,0)!=d.id||
       d.allowableClasses!=1024||!d.talentRank||d.talentRank>5)return false;
    const uint32_t rank=d.talentRank;
    if(d.talentId==821) {
        if(rank>2||d.id!=17049+rank||!localProgressionSourceMatches(t,row,
           {{4,464},{28,1},{35,101},{46,1},{68,0xffffffff},{71,6},{72,6},
            {74,1},{75,1},{80,rank*20-1},{81,rank-1},{86,1},{87,1},
            {95,108},{96,137},{110,8},{111,0xffffffff},{122,262144},{208,7},
            {216,0x3f800000},{217,0x3f800000},{218,0x3f800000},{225,1},
            {229,0x3f800000},{231,0x3f800000}}))return false;
        d.passiveTotalStatPct.fill(uint8_t(rank));
        d.passiveCastModifiers={};auto& mod=d.passiveCastModifiers[0];
        mod.active=true;mod.operation=8;mod.percentage=true;
        mod.amount=int32_t(rank*20);mod.mask={262144,0,0};
    } else if(d.talentId==824) {
        if(d.id!=17068+rank||!localProgressionSourceMatches(t,row,
           {{4,464},{28,1},{35,101},{40,21},{46,1},{68,0xffffffff},{71,6},{72,6},
            {74,1},{75,1},{80,uint32_t(-int32_t(rank*100)-1)},{81,rank*2-1},
            {86,1},{87,1},{95,107},{96,79},{107,32},{110,10},{111,1},
            {122,32},{208,7},{216,0x3f800000},{217,0x3f800000},{225,1},
            {229,0x3f800000},{231,0x3f800000}}))return false;
        d.passivePhysicalDamagePct=uint8_t(rank*2);
        d.passiveCastModifiers={};auto& mod=d.passiveCastModifiers[0];
        mod.active=true;mod.operation=10;mod.percentage=false;
        mod.amount=-int32_t(rank*100);mod.mask={32,0,0};
    } else if(d.talentId==826) {
        if(rank>3||d.id!=16832+rank||!localDruidProgressionSourceMatches(t,row,
           {{4,464},{28,1},{35,101},{40,rank==1?21u:32u},{46,1},{68,0xffffffff},
            {71,6},{74,1},{80,uint32_t(-int32_t(rank*10)-1)},{86,1},{95,108},
            {110,14},{122,3758096384u},{123,122880},{208,7},{216,0x3f800000},
            {225,1},{229,0x3f800000},{230,0x3f800000},{231,0x3f800000}}))return false;
        // AzerothCore 9c416aaa SpellInfoCorrections.cpp explicitly substitutes
        // duration entry 21 for 16834 and 16835; require both raw/corrected rows.
        d.durationMs=0;d.passiveCastModifiers={};auto& mod=d.passiveCastModifiers[0];
        mod.active=true;mod.operation=14;mod.percentage=true;
        mod.amount=-int32_t(rank*10);mod.mask={3758096384u,122880,0};
    } else return false;
    d.passive=true;d.buffSelfOnly=true;d.unsupportedReason.clear();return true;
}
}
