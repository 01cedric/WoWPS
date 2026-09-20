#pragma once
#include "game/local_arcane_import.hpp"
#include "game/local_ignite.hpp"
namespace wowee::game {
template<class Tables>
inline bool decodeClientIgniteTalent(const Tables& t,uint32_t row,LocalSpellDefinition& d,
                                     LocalSpellDefinition* internalDot=nullptr) {
    constexpr uint32_t ids[]={11119,11120,12846,12847,12848};
    if(d.talentId!=34||d.talentTab!=41||d.talentRow!=1||d.talentRank<1||d.talentRank>5||
       d.id!=ids[d.talentRank-1]||!d.clientSpell||d.allowableClasses!=128||
       std::any_of(d.talentPrerequisites.begin(),d.talentPrerequisites.end(),[](auto v){return v!=0;})||
       std::any_of(d.talentPrerequisiteRanks.begin(),d.talentPrerequisiteRanks.end(),[](auto v){return v!=0;}))return false;
    bool matched=false;
    switch(d.id) {
        case 11119:matched=localArcaneSourceMatches(t,row,d.id,{{4u,464u},{7u,67108864u},{28u,1u},{34u,327680u},{35u,100u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{86u,1u},{95u,4u},{208u,3u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 11120:matched=localArcaneSourceMatches(t,row,d.id,{{4u,464u},{7u,67108864u},{28u,1u},{34u,327680u},{35u,100u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{86u,1u},{95u,4u},{208u,3u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 12846:matched=localArcaneSourceMatches(t,row,d.id,{{4u,464u},{7u,67108864u},{28u,1u},{34u,327680u},{35u,100u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{86u,1u},{95u,4u},{208u,3u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 12847:matched=localArcaneSourceMatches(t,row,d.id,{{4u,464u},{7u,67108864u},{28u,1u},{34u,327680u},{35u,100u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{86u,1u},{95u,4u},{208u,3u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        case 12848:matched=localArcaneSourceMatches(t,row,d.id,{{4u,464u},{7u,67108864u},{28u,1u},{34u,327680u},{35u,100u},{39u,1u},{46u,1u},{68u,4294967295u},{71u,6u},{74u,1u},{86u,1u},{95u,4u},{122u,134217728u},{208u,3u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,1u}});break;
        default:return false;
    }
    const auto child=Tables::lookup(t.spellIndex,12654),duration=Tables::lookup(t.durationIndex,35),range=Tables::lookup(t.rangeIndex,13);
    if(!matched||child<0||duration<0||range<0||!localArcaneTablesMatch(t,false)||
       t.durations->getInt32(duration,1)!=4000||t.durations->getInt32(duration,2)||t.durations->getInt32(duration,3)!=4000||
       t.ranges->getFloat(range,1)||t.ranges->getFloat(range,2)||t.ranges->getFloat(range,3)!=50000||
       t.ranges->getFloat(range,4)!=50000||t.ranges->getUInt32(range,5)||
       !localArcaneSourceMatches(t,uint32_t(child),12654,{{2u,1u},{4u,8388608u},{5u,136u},{6u,4u},{7u,268697600u},{8u,1048960u},{9u,8388608u},{10u,536870912u},{28u,1u},{35u,101u},{38u,99u},{39u,99u},{40u,35u},{46u,13u},{68u,4294967295u},{71u,6u},{72u,6u},{74u,1u},{75u,1u},{80u,4294967295u},{81u,4294967295u},{86u,6u},{87u,6u},{95u,3u},{96u,4u},{98u,2000u},{208u,3u},{209u,134217728u},{211u,8u},{216u,1065353216u},{217u,1065353216u},{218u,1065353216u},{225u,4u}}))return false;
    d.passive=true;d.proc={};d.proc.effect=LocalProcEffect::Ignite;d.proc.spellId=12654;
    d.proc.flags=327680;d.proc.chance=100;d.proc.amount=8*d.talentRank;
    d.proc.triggerSchoolMask=4;d.proc.triggerSpellFamily=3;d.proc.hitMask=LocalProcHitCritical;
    d.proc.phaseMask=LocalProcPhaseHit;d.proc.spellTypeMask=1;d.proc.allowTriggered=true;
    d.proc.schoolMask=4;d.proc.spellFamily=3;d.proc.spellFamilyFlags={134217728,0,8};d.proc.range=50000;
    d.spellFamily=3;d.schoolMask=1;d.unsupportedReason.clear();
    if(internalDot) {
        LocalSpellDefinition dot;dot.id=12654;dot.name=t.spells->getString(uint32_t(child),136);
        dot.iconId=t.spells->getUInt32(uint32_t(child),133);dot.visualId=t.spells->getUInt32(uint32_t(child),131);
        dot.clientSpell=true;dot.triggeredOnly=true;dot.allowableClasses=128;dot.procParentTalentId=34;
        dot.durationMs=4000;dot.periodicIntervalMs=2000;dot.periodicEffectSlot=0;
        // DBC base points are -1: the callback supplies the base tick amount.
        dot.spellFamily=3;dot.spellFamilyFlags={134217728,0,8};dot.schoolMask=4;
        dot.baseLevel=99;dot.range=50000;dot.resourceType=0;dot.buffSelfOnly=false;
        *internalDot=std::move(dot);
    }
    return true;
}
}
