#pragma once
#include "game/local_gameplay.hpp"
#include <algorithm>
#include <cmath>

namespace wowee::game {
// AzerothCore 9c416aa, SpellMgr.h ProcAttributes. Unlisted bits cannot be
// interpreted as unconditional success by the local authority.
enum LocalProcAttribute : uint32_t {
    LocalProcRequireXpOrHonor=0x1,
    LocalProcTriggeredCanProc=0x2,
    LocalProcRequireManaCost=0x4,
    LocalProcRequireSpellMod=0x8,
    LocalProcUseStacksForCharges=0x10,
    LocalProcReduceAbove60=0x80,
    LocalProcExcludeItemCast=0x100
};
constexpr uint32_t LocalProcSupportedAttributes=0x19f;
inline bool validLocalProcAttributes(const LocalProcDefinition& p) {
    return !(p.attributesMask&~LocalProcSupportedAttributes) &&
        !(p.disableEffectsMask&~7u) && p.sourceEffectMask &&
        !(p.sourceEffectMask&~7u) && !(p.sourceEffectMask&(p.sourceEffectMask-1)) &&
        !p.hasUnsupportedConditions && !p.hasUnsupportedScript;
}
struct LocalProcAuraContext {
    uint32_t spellId=0;
    uint64_t generation=0;
    bool usingCharges=false;
    uint8_t stacks=0;
    uint64_t ownerGuid=0;
};
inline bool localProcAttributeEligible(const LocalProcDefinition& p,const LocalCombatEvent& event,
                                       LocalProcAuraContext aura) {
    if(!validLocalProcAttributes(p)||(p.disableEffectsMask&p.sourceEffectMask))return false;
    if((p.attributesMask&LocalProcRequireXpOrHonor)) {
        // Source only checks players and an existing action target. An
        // unresolved non-null identity must not become the null-target bypass.
        if(!event.actorLevel)return false;
        if(event.actorIsPlayer&&event.target&&
           (!event.actionTargetKnown||!event.actionTargetHonorOrXpEligible))return false;
    }
    // SpellMgr checks the original SpellInfo cost, including percentage cost.
    // A successful zero-cost Clearcasting cast still has this source cost.
    if((p.attributesMask&LocalProcRequireManaCost)&&(!event.spell||!event.sourceHasManaCost))return false;
    if((p.attributesMask&LocalProcExcludeItemCast)&&event.sourceItemCast)return false;
    if((p.attributesMask&LocalProcUseStacksForCharges)&&!aura.stacks)return false;
    if((p.attributesMask&LocalProcRequireSpellMod)&&
       (aura.usingCharges||(p.attributesMask&LocalProcUseStacksForCharges))) {
        // Applied-mod provenance is aura application identity, not merely
        // matching spell family. The supported charged-cost path records it
        // on the committed cast before preparation consumes the charge.
        if(!event.spell||!aura.ownerGuid||event.source!=aura.ownerGuid||
           event.appliedCostAuraSpell!=aura.spellId||!aura.generation||
           event.appliedCostAuraGeneration!=aura.generation)return false;
    }
    return true;
}
inline uint32_t localProcReducedChanceBasisPoints(const LocalProcDefinition& p,
                                                  const LocalCombatEvent& event,double chance) {
    if(!validLocalProcAttributes(p)||!std::isfinite(chance)||chance<=0)return 0;
    if(p.attributesMask&LocalProcReduceAbove60) {
        if(!event.actorLevel)return 0;
        if(event.actorLevel>60)chance*=std::max(0.0,1.0-double(event.actorLevel-60)/30.0);
    }
    return uint32_t(std::llround(std::clamp(chance,0.0,10000.0)));
}
inline void localPrepareProcConsumption(const LocalProcDefinition& p,const LocalCombatEvent& event,
                                        LocalStatAura& aura) {
    // Aura::PrepareProcToTrigger debits charges even for a stack-consuming
    // aura when both counters exist. Stacks themselves change after callback.
    if(p.charges&&aura.procCharges&&!event.sourceDoNotConsumeResources)--aura.procCharges;
    aura.procCooldownMs=p.cooldownMs;
}
inline void localFinalizeProcConsumption(const LocalProcDefinition& p,LocalStatAura& aura) {
    // Caller must first verify the prepared application identity, so callback
    // refresh/removal cannot spend a replacement aura's resources.
    if(p.attributesMask&LocalProcUseStacksForCharges) {
        if(aura.stacks)--aura.stacks;
        if(!aura.stacks)aura.remainingMs=0;
    } else if(p.charges&&!aura.procCharges)aura.remainingMs=0;
}
}
