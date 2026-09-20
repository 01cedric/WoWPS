#pragma once
#include "game/local_talents.hpp"
#include "game/local_proc_rules.hpp"

namespace wowee::game {
inline bool localClearcastingChild(const LocalSpellDefinition& d) {
    return d.clearcastingProfile==3 || d.clearcastingProfile==4;
}
inline bool localClearcastingReady(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                   const LocalSpellDefinition& child) {
    if(p.dead || !p.health || p.classId<1 || p.classId>11 || !validLocalTalents(p) ||
       !localClearcastingChild(child) || !child.unsupportedReason.empty() || !validLocalProc(child) ||
       !(child.allowableClasses&(1u<<(p.classId-1))))return false;
    const auto allocated=std::find_if(p.talents.begin(),p.talents.end(),[&](const auto& t){return t.first==child.procParentTalentId;});
    if(allocated==p.talents.end())return false;
    const auto* parent=localTalentSpell(c,allocated->first,allocated->second);
    return parent && parent->passive && parent->unsupportedReason.empty() && validLocalProc(*parent) &&
        parent->proc.effect==LocalProcEffect::ApplyOwnerAura && parent->proc.spellId==child.id &&
        parent->clearcastingProfile+2==child.clearcastingProfile &&
        (parent->allowableClasses&(1u<<(p.classId-1))) && localTalentPrerequisitesReady(p,c,*parent);
}
// This query is also used by the original UI. It never reserves or consumes a
// charge: only a successfully prepared authority cast can acquire an identity.
inline uint32_t localChargedSpellCost(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                      const LocalSpellDefinition& cast,uint32_t cost,uint32_t* appliedAura) {
    if(appliedAura)*appliedAura=0;
    if(!cost || (!cast.mana&&!cast.manaPercent) || cast.triggeredOnly || cast.passive ||
       !cast.spellFamily || !cast.unsupportedReason.empty())return cost;
    for(const auto& aura:p.statAuras) {
        if(!aura.remainingMs || aura.procCharges!=1 || aura.mapId!=p.mapId || aura.instanceId!=p.instanceId ||
           aura.casterGuid!=p.guid)continue;
        const auto* child=c.spell(aura.spellId);
        if(!child || !localClearcastingReady(p,c,*child) || aura.remainingMs>child->durationMs ||
           child->spellFamily!=cast.spellFamily)continue;
        bool match=false;
        for(size_t i=0;i<3;++i)match=match || (child->chargedCostMask[i]&cast.spellFamilyFlags[i]);
        if(!match)continue;
        if(appliedAura)*appliedAura=child->id;
        // Source Mage -1000% and Druid -100% both clamp the final cost to zero.
        return uint32_t(std::max(int64_t(0),int64_t(cost)*(100+child->chargedCostPct)/100));
    }
    return cost;
}
inline uint64_t nextLocalCostModGeneration(LocalRealmPlayer& p) {
    if(!++p.nextCostModGeneration)++p.nextCostModGeneration;
    return p.nextCostModGeneration;
}
inline void clearLocalPreparedCost(LocalRealmPlayer& p) {
    p.castCostPrepared=false;p.castPreparedCost=0;p.castCostModSpellId=0;p.castCostModGeneration=0;
}
inline void prepareLocalSpellCost(LocalRealmPlayer& p,uint32_t cost,uint32_t auraSpell) {
    p.castPreparedCost=cost;p.castCostPrepared=true;p.castCostModSpellId=0;p.castCostModGeneration=0;
    if(!auraSpell)return;
    for(auto& aura:p.statAuras)if(aura.spellId==auraSpell && aura.remainingMs && aura.procCharges) {
        if(!aura.costModGeneration)aura.costModGeneration=nextLocalCostModGeneration(p);
        p.castCostModSpellId=auraSpell;p.castCostModGeneration=aura.costModGeneration;return;
    }
}
}
