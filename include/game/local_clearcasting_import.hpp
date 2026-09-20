#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_proc_rules.hpp"
#include <algorithm>
#include <initializer_list>
#include <utility>

namespace wowee::game {
// Build 12340 profiles, paired with AzerothCore 9c416aaa spell_proc/scripts.
// Templates allow the normal importer to supply its indexed, transient DBC view.
template<class Tables>
inline bool clearcastingSourceMatches(const Tables& t,uint32_t row,
        std::initializer_list<std::pair<uint32_t,uint32_t>> populated) {
    for(uint32_t col=1;col<234;++col) {
        if(col>=131&&col<=203)continue; // presentation only
        uint32_t expected=0;
        for(const auto& [key,value]:populated)if(key==col){expected=value;break;}
        if(t.spells->getUInt32(row,col)!=expected)return false;
    }
    return true;
}

// Source script exclusions apply to whole spells, including their other effects.
// Custom DIRECT_DAMAGE/NO_INITIAL_THREAT bits mirror SpellMgr's effect-derived
// classification; currently unsupported effects are not made executable here.
template<class Tables>
inline void decodeClientOmenEventMetadata(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    const auto u=[&](uint32_t col){return t.spells->getUInt32(row,col);};
    const auto cast=Tables::lookup(t.castIndex,u(28));
    d.sourceRawCastTimeMs=cast>=0?uint32_t(std::max(0,t.casts->getInt32(cast,1))):0;
    bool forbidden=(u(4)&64u)!=0,shape=false,directOrNoThreat=false;
    for(unsigned effect=0;effect<3;++effect) {
        const auto kind=u(71+effect),aura=u(95+effect);
        forbidden|=kind==24||kind==30; // CREATE_ITEM / ENERGIZE
        shape|=aura==36;
        switch(kind) {
            case 2:case 17:case 31:case 58:case 121:case 10: // direct damage / heal
            case 8:case 62:case 67:case 9:case 136:case 137:case 30:case 75:case 24:
                directOrNoThreat=true;break;
            default:break;
        }
        switch(aura) {
            case 44:case 140:case 128:case 19:case 82:directOrNoThreat=true;break;
            default:break;
        }
    }
    if(u(208)==9&&u(1)==47)directOrNoThreat=true; // hunter aspect source exception
    d.omenProcEligible=!forbidden&&(directOrNoThreat?u(133)!=2862:(u(208)==7&&!shape));
    // The source script's melee-special branch accepts only next-swing spells;
    // current Omen ProcFlags excludes specials, but preserve that script branch.
    if(!forbidden&&u(213)==2)d.omenProcEligible=(u(4)&(4u|1024u))!=0;
}

template<class Tables>
inline bool decodeClientClearcastingTalent(const Tables& t,uint32_t row,LocalSpellDefinition& d,
                                          LocalSpellDefinition* internalAura=nullptr) {
    constexpr uint32_t mageRanks[]={11213,12574,12575,12576,12577};
    const bool druid=d.talentId==827;
    if(druid) {
        if(d.id!=16864||d.allowableClasses!=1024||d.talentRank!=1)return false;
        if(!clearcastingSourceMatches(t,row,{{2,1u},{4,192u},{7,67108864u},{28,1u},{34,81924u},
            {35,100u},{37,26u},{38,20u},{39,20u},{46,1u},{68,4294967295u},{71,6u},{74,1u},
            {80,4294967295u},{86,1u},{95,42u},{116,16870u},{205,133u},{206,1500u},{208,7u},
            {210,2097152u},{213,1u},{214,1u},{216,1065353216u},{217,1065353216u},
            {218,1065353216u},{225,8u},{229,1065353216u},{230,1065353216u}}))return false;
    } else {
        if(d.talentId!=75||d.allowableClasses!=128||d.talentRank<1||d.talentRank>5||
           d.id!=mageRanks[d.talentRank-1])return false;
        if(!clearcastingSourceMatches(t,row,{{4,464u},{6,8388608u},{7,67108864u},{28,1u},
            {34,87376u},{35,2u*d.talentRank},{39,1u},{46,1u},{68,4294967295u},{71,6u},
            {74,1u},{80,4294967295u},{86,1u},{87,1u},{95,42u},{116,12536u},{125,128u},
            {208,3u},{216,1065353216u},{217,1065353216u},{218,1065353216u},{225,64u}}))return false;
    }
    const uint32_t childId=druid?16870:12536;
    const auto child=Tables::lookup(t.spellIndex,childId);
    if(child<0)return false;
    if(!clearcastingSourceMatches(t,uint32_t(child),{{2,1u},{4,druid?262144u:327680u},{6,4u},
        {7,druid?1073938432u:1073741824u},{28,1u},{34,87376u},{35,100u},{36,1u},{38,10u},
        {39,10u},{40,8u},{46,6u},{68,4294967295u},{71,6u},{74,1u},
        {80,druid?4294967195u:4294966295u},{86,1u},{95,108u},{110,14u},
        {122,druid?14924799u:549591799u},{123,druid?126879699u:168000u},
        {124,druid?263168u:0u},{208,druid?7u:3u},{210,druid?2097152u:2u},{211,druid?0u:8u},
        {213,1u},{214,1u},{216,1065353216u},{217,1065353216u},{218,1065353216u},
        {225,druid?8u:64u}}))return false;
    const auto cast=Tables::lookup(t.castIndex,1),range=Tables::lookup(t.rangeIndex,1);
    const auto childRange=Tables::lookup(t.rangeIndex,6);
    const auto duration=Tables::lookup(t.durationIndex,8);
    if(cast<0||range<0||childRange<0||duration<0||t.casts->getInt32(cast,1)||t.casts->getInt32(cast,2)||
       t.casts->getInt32(cast,3)||t.ranges->getFloat(range,1)!=0||t.ranges->getFloat(range,2)!=0||
       t.ranges->getFloat(range,3)!=0||t.ranges->getFloat(range,4)!=0||t.ranges->getUInt32(range,5)||
       t.ranges->getFloat(childRange,1)!=0||t.ranges->getFloat(childRange,2)!=0||
       t.ranges->getFloat(childRange,3)!=100||t.ranges->getFloat(childRange,4)!=100||t.ranges->getUInt32(childRange,5)||
       t.durations->getInt32(duration,1)!=15000||t.durations->getInt32(duration,2)||
       t.durations->getInt32(duration,3)!=15000)return false;

    LocalSpellDefinition aura;aura.id=childId;aura.clientSpell=true;aura.triggeredOnly=true;
    aura.allowableClasses=d.allowableClasses;aura.buffSelfOnly=true;aura.durationMs=15000;
    aura.range=0;aura.baseLevel=10;aura.schoolMask=druid?8:64;aura.sourceDamageClass=1;
    aura.spellFamily=druid?7:3;aura.spellFamilyFlags={0,druid?2097152u:2u,druid?0u:8u};
    aura.procParentTalentId=d.talentId;aura.clearcastingProfile=druid?4:3;
    aura.chargedCostPct=druid?-100:-1000;
    aura.chargedCostMask=druid?std::array<uint32_t,3>{14924799u,126879699u,263168u}:
                                   std::array<uint32_t,3>{549591799u,168000u,0};
    aura.name=t.spells->getString(child,136);aura.iconId=t.spells->getUInt32(child,133);
    aura.proc.effect=LocalProcEffect::ConsumeSpellCostCharge;aura.proc.spellId=childId;
    aura.proc.attributesMask=LocalProcRequireManaCost|LocalProcRequireSpellMod;
    aura.proc.sourceEffectMask=1; // Source aura108 resides in DBC effect slot zero.
    aura.proc.flags=87376;aura.proc.chance=100;aura.proc.charges=1;aura.proc.amount=1;
    aura.proc.schoolMask=aura.schoolMask;aura.proc.spellFamily=aura.spellFamily;
    aura.proc.spellFamilyFlags=aura.spellFamilyFlags;aura.proc.phaseMask=LocalProcPhaseCast;
    aura.proc.hitMask=LocalProcHitNormal|LocalProcHitCritical|LocalProcHitAbsorb;
    aura.proc.triggerSpellFamily=aura.spellFamily;aura.proc.triggerSpellFamilyFlags=aura.chargedCostMask;
    LocalProcDefinition proc;proc.effect=LocalProcEffect::ApplyOwnerAura;proc.spellId=childId;
    proc.flags=druid?81924:87376;proc.chance=druid?100:2*d.talentRank;proc.ppm=druid?3.5f:0;
    proc.amount=1;proc.schoolMask=aura.schoolMask;proc.spellFamily=aura.spellFamily;
    proc.spellFamilyFlags=aura.spellFamilyFlags;proc.allowTriggered=true;
    proc.spellTypeMask=druid?7:1;proc.triggerSpellFamily=druid?0:3;
    proc.hitMask=LocalProcHitNormal|LocalProcHitCritical|LocalProcHitAbsorb;
    d.clearcastingProfile=druid?2:1;d.proc=proc;d.passive=true;d.buffSelfOnly=true;
    // Omen's source record has a GCD despite being a learned passive. Preserve
    // authored metadata; learning installs the talent without casting this aura.
    d.unsupportedReason.clear();
    if(!validLocalProc(d)||!validLocalProc(aura))return false;
    if(internalAura)*internalAura=std::move(aura);
    return true;
}
}
