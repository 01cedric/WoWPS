#pragma once
#include "game/local_gameplay.hpp"
#include "pipeline/dbc_loader.hpp"
#include <array>
#include <initializer_list>
#include <cstdint>
#include <utility>

namespace wowee::game::detail {
// Exact 12340 Ghost Wolf closure. The zero-amount OBS_MOD_HEALTH effect is
// a glyph modifier anchor, not free periodic healing. Passive 67116 is derived
// from active form state by localFormRunPercent; it cannot outlive the form.
// Source identities and form-table provenance: the source audit.
inline bool localGhostWolfRecord(const pipeline::DBCFile& spells,uint32_t row,uint32_t id) {
    std::array<uint32_t,234> expected{};
    if(id==2645) {
        for(const auto& [column,value]:std::initializer_list<std::pair<uint32_t,uint32_t>>{
            {0,2645u},{2,1u},{4,98304u},{5,132096u},{6,2u},{8,64u},
            {28,5u},{31,15u},{35,101u},{38,16u},{39,16u},{40,21u},
            {46,1u},{68,4294967295u},{71,6u},{72,6u},{73,6u},{74,1u},
            {75,1u},{76,1u},{80,4294967295u},{81,39u},{82,4294967295u},{86,1u},
            {87,1u},{88,1u},{95,36u},{96,31u},{97,20u},{100,5000u},
            {101,1065353216u},{102,1065353216u},{110,16u},{204,6u},{205,133u},{206,1500u},
            {208,11u},{209,2048u},{213,1u},{214,1u},{216,1065353216u},{217,1065353216u},
            {218,1065353216u},{225,8u}
        })expected[column]=value;
    }
    else if(id==67116) {
        for(const auto& [column,value]:std::initializer_list<std::pair<uint32_t,uint32_t>>{
            {0,67116u},{4,98688u},{5,268567552u},{6,2u},{8,64u},{28,1u},
            {35,101u},{38,16u},{39,16u},{40,21u},{46,1u},{68,4294967295u},
            {71,6u},{74,1u},{75,1u},{76,1u},{80,99u},{81,4294967295u},
            {82,4294967295u},{86,1u},{95,305u},{101,1065353216u},{205,133u},{208,11u},
            {216,1065353216u},{217,1065353216u},{218,1065353216u},{225,8u}
        })expected[column]=value;
    }
    else return false;
    if(spells.getFieldCount()!=234)return false;
    for(uint32_t column=0;column<234;++column) {
        if(column>=131&&column<=203)continue; // visual IDs, names, localized descriptions
        if(spells.getUInt32(row,column)!=expected[column])return false;
    }
    return true;
}
template<class Tables>
inline bool decodeClientGhostWolf(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    if(d.id!=2645)return false;
    const auto reject=[&](){if(d.unsupportedReason.empty())d.unsupportedReason="Unreviewed Ghost Wolf form/passive profile";return true;};
    const auto child=Tables::lookup(t.spellIndex,67116);
    if(!t.spells||child<0||!localGhostWolfRecord(*t.spells,row,2645)||
       !localGhostWolfRecord(*t.spells,uint32_t(child),67116))return reject();
    const auto cast=Tables::lookup(t.castIndex,5),passiveCast=Tables::lookup(t.castIndex,1);
    const auto duration=Tables::lookup(t.durationIndex,21),range=Tables::lookup(t.rangeIndex,1);
    if(!t.casts||!t.durations||!t.ranges||cast<0||passiveCast<0||duration<0||range<0)return reject();
    if(t.casts->getInt32(cast,1)!=2000||t.casts->getInt32(cast,2)||t.casts->getInt32(cast,3)!=2000||
       t.casts->getInt32(passiveCast,1)||t.casts->getInt32(passiveCast,2)||t.casts->getInt32(passiveCast,3)||
       t.durations->getInt32(duration,1)!=-1||t.durations->getInt32(duration,2)||
       t.durations->getInt32(duration,3)!=-1)return reject();
    for(uint32_t column=1;column<=5;++column)if(t.ranges->getUInt32(range,column))return reject();
    d.formId=16;d.allowableClasses=1u<<6;d.buffSelfOnly=true;
    return true;
}
}
