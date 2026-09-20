#pragma once
#include "game/local_gameplay.hpp"
#include <array>
#include <initializer_list>
#include <utility>
namespace wowee::game {
// Complete pinned 12340 records; no Anticipation/Toughness partial admission.
template<class Tables>
inline bool localStormstrikeRecordMatches(const Tables& t,uint32_t id) {
    if(!t.spells||t.spells->getFieldCount()!=234)return false;
    const auto row=Tables::lookup(t.spellIndex,id);if(row<0)return false;
    const auto matches=[&](std::initializer_list<std::pair<uint32_t,uint32_t>> populated) {
        if(t.spells->getUInt32(row,0)!=id)return false;
        for(uint32_t col=1;col<234;++col) {
            if(col>=131&&col<=203)continue;
            uint32_t expected=0;for(const auto& [key,value]:populated)if(key==col){expected=value;break;}
            if(t.spells->getUInt32(row,col)!=expected)return false;
        }
        return true;
    };
    bool valid=false;
    switch(id) {
        case 17364:valid=matches({{4u,327680u},{5u,512u},{7u,67108866u},{19u,1u},{28u,1u},{29u,8000u},{34u,139808u},{35u,100u},{36u,4u},{38u,40u},{39u,40u},{40u,29u},{46u,2u},{68u,2u},{69u,173555u},{71u,6u},{72u,64u},{73u,64u},{74u,1u},{76u,1u},{80u,19u},{82u,4294967295u},{86u,6u},{87u,6u},{88u,6u},{95u,271u},{110u,8u},{117u,32175u},{118u,32176u},{122u,1049603u},{123u,8192u},{204u,8u},{205u,133u},{206u,1500u},{208u,11u},{210u,16777232u},{213u,2u},{214u,2u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 32175:valid=matches({{4u,2424832u},{5u,512u},{7u,263680u},{8u,1u},{19u,1u},{28u,1u},{34u,139944u},{35u,100u},{36u,2u},{38u,40u},{39u,40u},{46u,2u},{68u,2u},{69u,173555u},{71u,58u},{86u,6u},{208u,11u},{210u,16u},{211u,5120u},{213u,2u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u},{229u,1065353216u}});break;
        case 32176:valid=matches({{4u,2424848u},{7u,17039872u},{8u,1u},{28u,1u},{35u,101u},{38u,40u},{39u,40u},{41u,3u},{46u,2u},{68u,2u},{69u,173555u},{71u,58u},{86u,6u},{208u,11u},{210u,16u},{211u,5120u},{213u,2u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u},{229u,1065353216u}});break;
        case 51521:valid=matches({{4u,464u},{28u,1u},{34u,16u},{35u,50u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{75u,1u},{80u,4294967295u},{81u,4294967295u},{86u,1u},{95u,42u},{110u,11u},{116u,63375u},{123u,16u},{126u,16u},{208u,11u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 51522:valid=matches({{4u,464u},{7u,65536u},{28u,1u},{34u,16u},{35u,100u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{75u,1u},{80u,4294967295u},{81u,4294967295u},{86u,1u},{95u,42u},{110u,11u},{116u,63375u},{123u,16u},{126u,16u},{208u,11u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 63375:valid=matches({{4u,537133056u},{6u,536870916u},{7u,1073741824u},{28u,1u},{39u,1u},{46u,6u},{68u,4294967295u},{71u,30u},{74u,1u},{80u,19u},{86u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,8u},{230u,1065353216u},{231u,1065353216u}});break;
        case 29082:valid=matches({{4u,464u},{28u,1u},{35u,101u},{39u,1u},{46u,1u},{68u,2u},{69u,42035u},{71u,6u},{74u,1u},{80u,3u},{86u,1u},{95u,79u},{110u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 29084:valid=matches({{4u,464u},{28u,1u},{35u,101u},{39u,1u},{46u,1u},{68u,2u},{69u,42035u},{71u,6u},{74u,1u},{80u,6u},{86u,1u},{95u,79u},{110u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 29086:valid=matches({{4u,464u},{28u,1u},{35u,101u},{39u,1u},{46u,1u},{68u,2u},{69u,42035u},{71u,6u},{74u,1u},{80u,9u},{86u,1u},{95u,79u},{110u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 51883:valid=matches({{4u,464u},{28u,1u},{35u,101u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{75u,1u},{76u,1u},{80u,32u},{81u,4294967295u},{82u,4294967295u},{86u,1u},{95u,268u},{110u,3u},{113u,3u},{122u,3093042200u},{123u,3192u},{208u,11u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 51884:valid=matches({{4u,464u},{28u,1u},{35u,101u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{75u,1u},{76u,1u},{80u,65u},{81u,4294967295u},{82u,4294967295u},{86u,1u},{95u,268u},{110u,3u},{113u,3u},{122u,3093042200u},{123u,3192u},{208u,11u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 51885:valid=matches({{4u,464u},{28u,1u},{35u,101u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{75u,1u},{76u,1u},{80u,99u},{81u,4294967295u},{82u,4294967295u},{86u,1u},{95u,268u},{110u,3u},{113u,3u},{122u,3093042200u},{123u,3192u},{208u,11u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 16268:valid=matches({{4u,159646096u},{5u,2147483648u},{28u,1u},{35u,101u},{38u,1u},{46u,1u},{68u,4294967295u},{71u,36u},{72u,36u},{74u,6u},{75u,1u},{81u,4294967295u},{86u,1u},{87u,1u},{101u,1065353216u},{116u,18848u},{117u,36591u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 18848:valid=matches({{4u,80u},{28u,1u},{35u,101u},{38u,1u},{39u,1u},{46u,1u},{68u,2u},{69u,173555u},{71u,22u},{74u,6u},{101u,1065353216u},{110u,90u},{129u,16u},{208u,11u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u},{229u,1065353216u}});break;
        case 36591:valid=matches({{4u,464u},{28u,1u},{35u,101u},{38u,1u},{39u,1u},{46u,1u},{68u,2u},{69u,173555u},{72u,6u},{75u,1u},{81u,4294967265u},{87u,1u},{96u,10u},{111u,127u},{129u,16u},{208u,11u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 30798:valid=matches({{4u,159646096u},{5u,2147483648u},{28u,1u},{35u,101u},{46u,1u},{68u,4294967295u},{71u,36u},{86u,1u},{116u,674u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u},{229u,1065353216u}});break;
        case 674:valid=matches({{4u,80u},{28u,1u},{35u,101u},{46u,1u},{68u,4294967295u},{71u,40u},{86u,1u},{213u,1u},{214u,1u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 30816:valid=matches({{4u,464u},{7u,16777216u},{28u,1u},{35u,101u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{80u,1u},{86u,1u},{95u,54u},{208u,8u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u},{229u,1065353216u}});break;
        case 30818:valid=matches({{4u,464u},{7u,16777216u},{28u,1u},{35u,101u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{80u,3u},{86u,1u},{95u,54u},{208u,8u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u},{229u,1065353216u}});break;
        case 30819:valid=matches({{4u,464u},{7u,16777216u},{28u,1u},{35u,101u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{80u,5u},{86u,1u},{95u,54u},{208u,8u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u},{229u,1065353216u}});break;
        case 43338:valid=matches({{4u,464u},{7u,67108864u},{28u,1u},{34u,20u},{35u,100u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{80u,4294967250u},{86u,1u},{95u,108u},{110u,14u},{122u,2416967680u},{208u,11u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        default:return false;
    }
    if(!valid||!t.casts||!t.ranges||!t.durations)return false;
    const auto cast=Tables::lookup(t.castIndex,1);if(cast<0)return false;
    for(uint32_t col=1;col<=3;++col)if(t.casts->getUInt32(cast,col))return false;
    const auto rangeId=t.spells->getUInt32(row,46);const auto range=Tables::lookup(t.rangeIndex,rangeId);
    if(range<0||t.ranges->getFloat(range,1)||t.ranges->getFloat(range,2)||
       t.ranges->getFloat(range,3)!=(rangeId==2?5.0f:rangeId==6?100.0f:0.0f)||
       t.ranges->getFloat(range,4)!=(rangeId==2?5.0f:rangeId==6?100.0f:0.0f)||
       t.ranges->getUInt32(range,5)!=(rangeId==2?1u:0u))return false;
    if(t.spells->getUInt32(row,40)) {
        const auto duration=Tables::lookup(t.durationIndex,29);
        if(duration<0||t.durations->getInt32(duration,1)!=12000||
           t.durations->getInt32(duration,2)||t.durations->getInt32(duration,3)!=12000)return false;
    }
    return true;
}
template<class Tables>
inline bool decodeClientStormstrike(const Tables& t,uint32_t row,LocalSpellDefinition& d,
                                    std::array<LocalSpellDefinition,3>* internalSpells=nullptr) {
    if(!t.spells||!d.clientSpell||d.allowableClasses!=64||!d.talentRank||
       t.spells->getUInt32(row,0)!=d.id||!localStormstrikeRecordMatches(t,d.id))return false;
    const auto rank=d.talentRank;
    const auto child=[&](uint32_t id,uint8_t profile) {
        const auto r=Tables::lookup(t.spellIndex,id);LocalSpellDefinition s;
        s.id=id;s.clientSpell=true;s.triggeredOnly=true;s.allowableClasses=64;
        s.stormstrikeProfile=profile;s.resourceType=0;s.baseLevel=profile==5?1:40;
        s.name=t.spells->getString(r,136);s.iconId=t.spells->getUInt32(r,133);
        s.visualId=t.spells->getUInt32(r,131);s.schoolMask=1;
        if(profile==5){s.buffSelfOnly=true;s.range=100;s.schoolMask=8;}
        else {
            s.weaponDamage=true;s.weaponPercent=100;s.normalizedWeapon=false;s.buffSelfOnly=false;s.directEffectSlot=0;
            s.range=5;s.sourceDamageClass=2;s.sourceNotAProc=true;s.spellFamily=11;s.spellFamilyFlags={0,16,5120};
            s.requiredItemClass=2;s.requiredItemSubclasses=173555;
            s.requiresMainHand=profile==2;s.requiresOffHand=profile==3;
            s.procParentTalentId=901;
        }
        return s;
    };
    if(d.talentId==2083) { // Mental Dexterity: total current Intellect -> melee AP.
        if(rank>3||d.id!=51882+rank)return false;
        constexpr uint8_t amount[]={33,66,100};d.passiveIntellectAttackPowerPct=amount[rank-1];
    } else if(d.talentId==1643) { // Weapon Mastery, fitting hand only.
        constexpr uint32_t ids[]={29082,29084,29086};constexpr uint8_t amount[]={4,7,10};
        if(rank>3||d.id!=ids[rank-1])return false;
        d.passivePhysicalDamagePct=amount[rank-1];d.requiredItemClass=2;
        d.requiredItemSubclasses=42035;d.requiredInventoryTypes=0;
    } else if(d.talentId==616) { // Spirit Weapons: both learned child capabilities.
        if(rank!=1||d.id!=16268||!localStormstrikeRecordMatches(t,18848)||
           !localStormstrikeRecordMatches(t,36591))return false;
        d.passiveCanParry=true;d.passiveSchoolThreatPercent=-30;d.passiveSchoolThreatMask=127;
        d.requiredItemClass=2;d.requiredItemSubclasses=173555;d.requiredInventoryTypes=0;
    } else if(d.talentId==617) { // Shamanistic Focus: unconditional authored shock cost modifier.
        if(rank!=1||d.id!=43338)return false;
        d.passiveCastModifiers={};auto& m=d.passiveCastModifiers[0];
        m.active=true;m.operation=14;m.percentage=true;m.amount=-45;m.mask={2416967680u,0,0};
        d.spellFamily=11;
    } else if(d.talentId==1690) { // Dual Wield: learned effect40, no fabricated offhand weapon.
        if(rank!=1||d.id!=30798||!localStormstrikeRecordMatches(t,674))return false;
        d.passiveCanDualWield=true;
    } else if(d.talentId==1692) { // +2/4/6 hit only with the source offhand requirement.
        constexpr uint32_t ids[]={30816,30818,30819};if(rank>3||d.id!=ids[rank-1])return false;
        d.passiveDualWieldHitPct=uint8_t(rank*2);d.requiresOffHand=true;
    } else if(d.talentId==901) {
        if(rank!=1||d.id!=17364||!localStormstrikeRecordMatches(t,32175)||
           !localStormstrikeRecordMatches(t,32176))return false;
        d.stormstrikeProfile=1;d.damage=d.damageMax=0;d.weaponDamage=false;
        d.buffSelfOnly=false;d.passive=false;d.resourceType=0;d.mana=0;d.manaPercent=8;
        d.baseLevel=40;d.castTimeMs=d.sourceRawCastTimeMs=0;d.range=5;d.cooldownMs=8000;
        d.globalCooldownMs=1500;d.durationMs=12000;d.sourceDamageClass=2;d.schoolMask=1;
        d.spellFamily=11;d.spellFamilyFlags={0,16777232,0};
        d.requiredItemClass=2;d.requiredItemSubclasses=173555;d.requiredInventoryTypes=0;
        if(internalSpells){(*internalSpells)[0]=child(32175,2);(*internalSpells)[1]=child(32176,3);}
        d.unsupportedReason.clear();return true;
    } else if(d.talentId==2054) {
        if(rank>2||d.id!=51520+rank||!localStormstrikeRecordMatches(t,63375))return false;
        d.stormstrikeProfile=4;d.stormstrikeManaChancePct=uint8_t(rank*50);
        LocalProcDefinition p;p.effect=LocalProcEffect::RestoreMana;p.spellId=63375;p.amount=20;
        p.flags=16;p.chance=uint8_t(rank*50);p.phaseMask=LocalProcPhaseCast;
        p.spellTypeMask=7;p.hitMask=LocalProcSupportedHits;p.triggerSpellFamily=11;p.triggerSpellFamilyFlags={0,16777216,0};
        p.schoolMask=8;p.resourceType=0;d.proc=p;
        if(internalSpells)(*internalSpells)[2]=child(63375,5);
    } else return false;
    d.passive=true;d.buffSelfOnly=true;d.unsupportedReason.clear();return true;
}
}
