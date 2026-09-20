#pragma once
#include "game/local_gameplay.hpp"
#include <initializer_list>
#include <utility>
namespace wowee::game {
// AzerothCore 9c416aaa and client build12340. Every functional column of
// BOTH ward effects and the Molten Shields modifier is required.
template<class Tables> inline bool localWardRecordMatches(const Tables& t,uint32_t id) {
    if(!t.spells||t.spells->getFieldCount()!=234)return false;
    const auto row=Tables::lookup(t.spellIndex,id);if(row<0)return false;
    const auto match=[&](std::initializer_list<std::pair<uint32_t,uint32_t>> expected) {
        for(uint32_t column=0;column<234;++column) {
            if(column>=131&&column<=203)continue;
            uint32_t value=0;for(const auto& entry:expected)if(entry.first==column){value=entry.second;break;}
            if(t.spells->getUInt32(row,column)!=value)return false;
        }
        return true;
    };
    bool valid=false;
    switch(id) {
        case 543:valid=match({{0u,543u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,29u},{38u,20u},{39u,20u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,164u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,4u},{111u,4u},{204u,16u},{205u,133u},{206u,1500u},{208u,3u},{209u,8u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,4u}});break;
        case 8457:valid=match({{0u,8457u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,39u},{38u,30u},{39u,30u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{80u,289u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,4u},{111u,4u},{204u,16u},{205u,133u},{206u,1500u},{208u,3u},{209u,8u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,4u}});break;
        case 8458:valid=match({{0u,8458u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,49u},{38u,40u},{39u,40u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,469u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,4u},{111u,4u},{204u,16u},{205u,133u},{206u,1500u},{208u,3u},{209u,8u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,4u}});break;
        case 10223:valid=match({{0u,10223u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,59u},{38u,50u},{39u,50u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,674u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,4u},{111u,4u},{204u,16u},{205u,133u},{206u,1500u},{208u,3u},{209u,8u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,4u}});break;
        case 10225:valid=match({{0u,10225u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,68u},{38u,60u},{39u,60u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,874u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,4u},{111u,4u},{204u,16u},{205u,133u},{206u,1500u},{208u,3u},{209u,8u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,4u}});break;
        case 27128:valid=match({{0u,27128u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,78u},{38u,69u},{39u,69u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,1124u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,4u},{111u,4u},{204u,16u},{205u,133u},{206u,1500u},{208u,3u},{209u,8u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,4u}});break;
        case 43010:valid=match({{0u,43010u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,87u},{38u,78u},{39u,78u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,1949u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,4u},{111u,4u},{204u,16u},{205u,133u},{206u,1500u},{208u,3u},{209u,8u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,4u}});break;
        case 6143:valid=match({{0u,6143u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,31u},{38u,22u},{39u,22u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,164u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,16u},{111u,16u},{204u,14u},{205u,133u},{206u,1500u},{208u,3u},{209u,256u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,16u}});break;
        case 8461:valid=match({{0u,8461u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,41u},{38u,32u},{39u,32u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,289u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,16u},{111u,16u},{204u,14u},{205u,133u},{206u,1500u},{208u,3u},{209u,256u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,16u}});break;
        case 8462:valid=match({{0u,8462u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,51u},{38u,42u},{39u,42u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,469u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,16u},{111u,16u},{204u,14u},{205u,133u},{206u,1500u},{208u,3u},{209u,256u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,16u}});break;
        case 10177:valid=match({{0u,10177u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,59u},{38u,52u},{39u,52u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,674u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,16u},{111u,16u},{204u,14u},{205u,133u},{206u,1500u},{208u,3u},{209u,256u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,16u}});break;
        case 28609:valid=match({{0u,28609u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,69u},{38u,60u},{39u,60u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,874u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,16u},{111u,16u},{204u,16u},{205u,133u},{206u,1500u},{208u,3u},{209u,256u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,16u}});break;
        case 32796:valid=match({{0u,32796u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,78u},{38u,70u},{39u,70u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,1124u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,16u},{111u,16u},{204u,14u},{205u,133u},{206u,1500u},{208u,3u},{209u,256u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,16u}});break;
        case 43012:valid=match({{0u,43012u},{1u,56u},{2u,1u},{4u,65536u},{28u,1u},{30u,30000u},{31u,8u},{35u,101u},{37u,88u},{38u,79u},{39u,79u},{40u,9u},{46u,1u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,1949u},{81u,4294967295u},{86u,1u},{87u,1u},{95u,69u},{96u,74u},{102u,1065353216u},{110u,16u},{111u,16u},{204u,14u},{205u,133u},{206u,1500u},{208u,3u},{209u,256u},{211u,8u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,16u}});break;
        case 11094:valid=match({{0u,11094u},{4u,262352u},{28u,1u},{35u,101u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{80u,14u},{86u,1u},{95u,107u},{110u,12u},{122u,264u},{208u,3u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 13043:valid=match({{0u,13043u},{4u,262352u},{28u,1u},{35u,101u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{80u,29u},{86u,1u},{95u,107u},{110u,12u},{122u,264u},{208u,3u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        default:return false;
    }
    if(!valid||!t.casts||!t.ranges||!t.durations)return false;
    // Molten Armor script identifies this modifier by icon16, so this icon
    // is functional metadata, unlike the ordinary presentation columns.
    if((id==11094||id==13043)&&t.spells->getUInt32(row,133)!=16)return false;
    const auto cast=Tables::lookup(t.castIndex,1),range=Tables::lookup(t.rangeIndex,1);
    if(cast<0||range<0)return false;
    for(uint32_t i=1;i<4;++i)if(t.casts->getUInt32(cast,i))return false;
    for(uint32_t i=1;i<6;++i)if(t.ranges->getUInt32(range,i))return false;
    if(id!=11094&&id!=13043) {
        const auto duration=Tables::lookup(t.durationIndex,9);
        if(duration<0||t.durations->getInt32(duration,1)!=30000||
           t.durations->getInt32(duration,2)||t.durations->getInt32(duration,3)!=30000)return false;
    }
    return true;
}
template<class Tables> inline bool decodeClientWard(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    if(!d.clientSpell||d.allowableClasses!=128||d.talentRank||
       !localWardRecordMatches(t,d.id)||t.spells->getUInt32(row,0)!=d.id)return false;
    constexpr uint32_t fire[]={543,8457,8458,10223,10225,27128,43010};
    constexpr uint32_t frost[]={6143,8461,8462,10177,28609,32796,43012};
    uint8_t profile=0;uint32_t next=0;
    for(unsigned i=0;i<7;++i) {
        if(fire[i]==d.id){profile=1;next=i<6?fire[i+1]:0;}
        if(frost[i]==d.id){profile=2;next=i<6?frost[i+1]:0;}
    }
    if(!profile||(d.supercededBySpell&&d.supercededBySpell!=next))return false;
    d.wardProfile=profile;d.supercededBySpell=next;
    d.buffAbsorb=uint32_t(t.spells->getInt32(row,80)+1);
    d.absorbSchoolMask=profile==1?4u:16u;d.buffSelfOnly=true;
    d.durationMs=30000;d.maxAuraStacks=1;
    return true;
}
template<class Tables> inline bool decodeClientMoltenShields(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    if(!d.clientSpell||d.allowableClasses!=128||d.talentId!=24||d.talentTab!=41||
       d.talentRow!=3||d.talentRank<1||d.talentRank>2||
       d.id!=(d.talentRank==1?11094u:13043u)||t.spells->getUInt32(row,0)!=d.id||
       !localWardRecordMatches(t,d.id))return false;
    d.passive=true;d.buffSelfOnly=true;d.spellFamily=3;
    auto& mod=d.passiveCastModifiers[0];mod.active=true;mod.operation=12;
    mod.percentage=false;mod.amount=15*d.talentRank;mod.mask={264,0,0};
    d.moltenShieldsChancePct=uint8_t(50*d.talentRank);
    d.unsupportedReason.clear();return true;
}
}
