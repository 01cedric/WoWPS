#pragma once
#include "game/local_progression_import.hpp"

namespace wowee::game {
// Feral Swiftness's scripted dodge is inseparable from its Cat speed aura.
// Validate the actual helper row instead of silently inventing a second effect.
template<class Tables>
inline bool localFeralSwiftnessHelperMatches(const Tables& t,uint32_t rank) {
    if(rank<1||rank>2)return false;
    const auto row=Tables::lookup(t.spellIndex,rank==1?24867u:24864u);
    return row>=0&&t.spells->getUInt32(row,0)==(rank==1?24867u:24864u)&&localProgressionSourceMatches(t,uint32_t(row),
        {{4,128},{5,1024},{8,2097152},{12,145},{28,1},{35,101},{38,8},{39,8},
         {40,21},{41,3},{46,1},{68,0xffffffff},{71,6},{74,1},{80,rank*2-1},
         {86,1},{95,49},{208,7},{216,0x3f800000},{217,0x3f800000},
         {218,0x3f800000},{225,1}});
}
// Fifteen complete 12340 talent ranks: each gameplay column is required and
// all authored effects are retained. Ordinary tier/prerequisite learning owns
// point spending; this decoder never grants points or bypasses Primal Fury.
template<class Tables>
inline bool decodeClientFeralProgressionTalent(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    if(!t.spells||t.spells->getFieldCount()!=234||t.spells->getUInt32(row,0)!=d.id||
       d.allowableClasses!=1024||!d.talentRank||d.talentRank>5)return false;
    const uint32_t rank=d.talentRank;
    if(d.talentId==796) {
        if(d.id!=16933+rank||!localProgressionSourceMatches(t,row,
            {{4,464},{28,1},{35,101},{46,1},{68,0xffffffff},{71,6},{72,6},
             {74,1},{75,1},{80,uint32_t(-int32_t(rank*10)-1)},{81,uint32_t(-int32_t(rank)-1)},
             {86,1},{87,1},{95,107},{96,107},{110,14},{111,14},
             {122,2048},{123,1048640},{125,4096},{126,1024},{127,263168},{208,7},
             {216,0x3f800000},{217,0x3f800000},{218,0x3f800000},{225,1},
             {229,0x3f800000},{230,0x3f800000},{231,0x3f800000}}))return false;
        d.passiveCastModifiers={};
        auto& rage=d.passiveCastModifiers[0];rage.active=true;rage.operation=14;
        rage.amount=-int32_t(rank*10);rage.mask={2048,1048640,0};
        auto& energy=d.passiveCastModifiers[1];energy.active=true;energy.operation=14;
        energy.amount=-int32_t(rank);energy.mask={4096,1024,263168};
    } else if(d.talentId==805) {
        if(rank>2||d.id!=16997+rank||!localProgressionSourceMatches(t,row,
            {{4,464},{28,1},{35,101},{46,1},{68,0xffffffff},{71,6},{72,6},{73,6},
             {74,1},{75,1},{76,1},{80,rank*10-1},{81,rank*10-1},{82,rank*10-1},
             {86,1},{87,1},{88,1},{95,108},{96,108},{97,108},{111,22},{112,23},
             {122,6144},{124,262144},{125,4096},{129,1088},{208,7},
             {216,0x3f800000},{217,0x3f800000},{218,0x3f800000},{225,1},
             {229,0x3f800000},{230,0x3f800000}}))return false;
        d.passiveCastModifiers={};
        constexpr uint8_t operations[]={0,22,23};
        constexpr std::array<uint32_t,3> masks[]={{{6144,0,262144}},{{4096,0,0}},{{0,1088,0}}};
        for(unsigned i=0;i<3;++i) {auto& m=d.passiveCastModifiers[i];m.active=true;
            m.operation=operations[i];m.percentage=true;m.amount=int32_t(rank*10);m.mask=masks[i];}
    } else if(d.talentId==794) {
        if(rank>3||d.id!=16928+rank||!localProgressionSourceMatches(t,row,
            {{4,464},{28,1},{35,101},{46,1},{68,0xffffffff},{71,6},{74,1},
             {80,rank*3},{86,1},{95,142},{110,1},{216,0x3f800000},{217,0x3f800000},
             {218,0x3f800000},{225,1},{230,0x3f800000},{231,0x3f800000}}))return false;
        d.passiveEquipmentArmorPct=uint8_t(rank*3+1);
    } else if(d.talentId==798) {
        if(rank>3||d.id!=16941+rank||!localProgressionSourceMatches(t,row,
            {{4,464},{12,145},{28,1},{35,101},{40,21},{46,1},{68,0xffffffff},
             {71,6},{74,1},{80,rank*2-1},{86,1},{95,52},{216,0x3f800000},
             {225,1},{229,0x3f800000},{230,0x3f800000},{231,0x3f800000}}))return false;
        d.passiveFeralCritPct=uint8_t(rank*2);
    } else if(d.talentId==807) {
        if(rank>2||d.id!=(rank==1?17002u:24866u)||!localProgressionSourceMatches(t,row,
            {{4,464},{5,rank==2?0x80000000u:0u},{12,1},{28,1},{35,101},{46,1},
             {68,0xffffffff},{71,6},{74,1},{80,rank*15-1},{86,1},{95,31},{208,7},
             {216,0x3f800000},{217,0x3f800000},{218,0x3f800000},{225,1},
             {229,0x3f800000},{230,0x3f800000},{231,0x3f800000}})||
             !localFeralSwiftnessHelperMatches(t,rank))return false;
        d.passiveCatRunPct=uint8_t(rank*15);d.passiveFeralDodgePct=uint8_t(rank*2);
    } else return false;
    d.passive=true;d.buffSelfOnly=true;d.unsupportedReason.clear();return true;
}
}
