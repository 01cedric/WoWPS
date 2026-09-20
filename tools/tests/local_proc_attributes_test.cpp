#include "game/local_proc_rules.hpp"
#include "local_group_rewards_fixture.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
static void runtimeStacks(bool preserveCharges) {
    auto content=rewardContent();content->spells[0].damage=0;content->spells[0].heal=2;
    content->spells[0].clientSpell=true;content->spells[0].sourceDamageClass=1;
    content->spells[0].sourceDoNotConsumeResources=preserveCharges;
    auto owner=rewardPlayer(1);owner.classId=8;owner.health=20;owner.maxMana=100;owner.mana=0;
    for(uint32_t id:{100u,101u}) {
        LocalSpellDefinition d;d.id=id;d.name="Runtime stack proc fixture";d.durationMs=60000;d.maxAuraStacks=2;
        d.proc.effect=id==100?LocalProcEffect::HealOwner:LocalProcEffect::RestoreMana;
        d.proc.spellId=id+1000;d.proc.flags=0x4000;d.proc.amount=3;d.proc.chance=100;
        d.proc.charges=2;d.proc.allowTriggered=true;d.proc.attributesMask=LocalProcUseStacksForCharges;
        assert(validLocalProc(d));content->spells.push_back(d);
        LocalStatAura a{d.id,d.durationMs,0,0,owner.guid};a.stacks=2;a.procCharges=2;
        a.procAmountSnapshot=3;a.hasProcAmountSnapshot=true;owner.statAuras.push_back(a);
    }
    LocalGameplay game;game.useContent(content);std::string result;
    uint64_t seen=0;
    for(unsigned cast=0;cast<2;++cast) {
        owner.globalCooldownMs=0;owner.cooldowns.clear();
        assert(game.execute(owner,{LocalAction::CastSpell,owner.guid,1},{&owner},result));
        unsigned callbacks=0;uint64_t parent=0;
        for(const auto& event:game.combatEvents())if(event.sequence>seen) {
            if(event.kind==LocalCombatEventKind::DirectHeal)parent=event.sequence;
            if(event.kind==LocalCombatEventKind::ProcHeal||event.kind==LocalCombatEventKind::ProcMana) {
                ++callbacks;assert(parent&&event.parentSequence==parent&&event.procDepth==1);
            }
            seen=std::max(seen,event.sequence);
        }
        assert(callbacks==2); // Nested heal cannot consume an additional stack.
        if(!cast) {
            assert(owner.statAuras.size()==2);
            for(const auto& aura:owner.statAuras) {
                assert(aura.stacks==1&&aura.remainingMs);
                assert(aura.procCharges==(preserveCharges?2:1));
            }
        } else assert(owner.statAuras.empty()); // Stack expiry also with unspent charges.
    }
}
static void runtimeManaAttribute(bool sourceCost,bool disabled,bool requireProvenance=false) {
    auto content=rewardContent();content->spells[0].damage=0;content->spells[0].heal=2;
    content->spells[0].clientSpell=true;content->spells[0].sourceDamageClass=1;
    content->spells[0].mana=sourceCost?1:0;
    auto owner=rewardPlayer(1);owner.classId=8;owner.health=20;owner.maxMana=100;owner.mana=20;
    LocalSpellDefinition d;d.id=100;d.name="Runtime source-cost proc fixture";d.durationMs=60000;
    d.proc.effect=LocalProcEffect::RestoreMana;d.proc.spellId=1100;d.proc.flags=0x4000;
    d.proc.amount=3;d.proc.chance=100;d.proc.charges=1;
    d.proc.attributesMask=requireProvenance?LocalProcRequireSpellMod:LocalProcRequireManaCost;
    d.proc.sourceEffectMask=2;d.proc.disableEffectsMask=disabled?2:1;
    assert(validLocalProc(d));content->spells.push_back(d);
    LocalStatAura a{d.id,d.durationMs,0,0,owner.guid};a.procCharges=1;a.costModGeneration=12;
    a.procAmountSnapshot=3;a.hasProcAmountSnapshot=true;owner.statAuras.push_back(a);
    LocalGameplay game;game.useContent(content);std::string result;
    assert(game.execute(owner,{LocalAction::CastSpell,owner.guid,1},{&owner},result));
    unsigned callbacks=0;for(const auto& event:game.combatEvents())if(event.kind==LocalCombatEventKind::ProcMana)++callbacks;
    const bool expected=sourceCost&&!disabled&&!requireProvenance;
    assert(callbacks==unsigned(expected)&&owner.statAuras.empty()==expected);
    if(!expected)assert(owner.statAuras[0].procCharges==1);
}
int main() {
    runtimeStacks(false);runtimeStacks(true);
    runtimeManaAttribute(false,false);runtimeManaAttribute(true,false);runtimeManaAttribute(true,true);
    runtimeManaAttribute(true,false,true);

    LocalProcDefinition p;p.flags=4;p.chance=60;
    LocalCombatEvent e;e.source=1;e.target=2;e.attempted=e.effective=10;e.actorLevel=60;e.actorIsPlayer=true;
    LocalProcAuraContext a{100,12,true,2,1};
    assert(validLocalProcAttributes(p)&&localProcAttributeEligible(p,e,a));
    for(uint32_t invalid:{0x20u,0x40u,0x200u,0x80000000u}) {
        p.attributesMask=invalid;assert(!validLocalProcAttributes(p)&&!validLocalProcFilters(p));
    }
    p.attributesMask=LocalProcRequireXpOrHonor;
    assert(!localProcAttributeEligible(p,e,a));
    e.actionTargetKnown=true;assert(!localProcAttributeEligible(p,e,a));
    e.actionTargetHonorOrXpEligible=true;assert(localProcAttributeEligible(p,e,a));
    e.actionTargetHonorOrXpEligible=false;e.target=0;assert(localProcAttributeEligible(p,e,a));
    e.target=2;e.actorIsPlayer=false;assert(localProcAttributeEligible(p,e,a));
    e.actorLevel=0;assert(!localProcAttributeEligible(p,e,a));e.actorLevel=60;e.actorIsPlayer=true;
    p.attributesMask=LocalProcRequireManaCost;
    assert(!localProcAttributeEligible(p,e,a));e.sourceHasManaCost=true;
    assert(!localProcAttributeEligible(p,e,a));e.spell=200;assert(localProcAttributeEligible(p,e,a));
    p.attributesMask=LocalProcExcludeItemCast;e.sourceItemCast=true;
    assert(!localProcAttributeEligible(p,e,a));e.sourceItemCast=false;assert(localProcAttributeEligible(p,e,a));
    p.attributesMask=LocalProcRequireSpellMod;
    assert(!localProcAttributeEligible(p,e,a));e.appliedCostAuraSpell=100;e.appliedCostAuraGeneration=12;
    assert(localProcAttributeEligible(p,e,a));
    for(auto bad:{LocalProcAuraContext{101,12,true,2,1},LocalProcAuraContext{100,13,true,2,1},
                  LocalProcAuraContext{100,0,true,2,1},LocalProcAuraContext{100,12,true,2,2}})
        assert(!localProcAttributeEligible(p,e,bad));
    e.appliedCostAuraSpell=0;a.usingCharges=false;assert(localProcAttributeEligible(p,e,a));
    p.attributesMask|=LocalProcUseStacksForCharges;assert(!localProcAttributeEligible(p,e,a));
    e.appliedCostAuraSpell=100;assert(localProcAttributeEligible(p,e,a));a.stacks=0;
    assert(!localProcAttributeEligible(p,e,a));a.stacks=2;
    p.attributesMask=0;
    for(unsigned bit=1;bit<=4;bit*=2) {
        p.sourceEffectMask=bit;p.disableEffectsMask=bit;assert(!localProcAttributeEligible(p,e,a));
        p.disableEffectsMask=7^bit;assert(localProcAttributeEligible(p,e,a));
    }
    p.disableEffectsMask=8;assert(!validLocalProcAttributes(p));p.disableEffectsMask=0;
    for(unsigned invalid:{0u,3u,7u,8u}){p.sourceEffectMask=invalid;assert(!validLocalProcAttributes(p));}
    p.sourceEffectMask=1;p.hasUnsupportedConditions=true;assert(!validLocalProcAttributes(p));
    p.hasUnsupportedConditions=false;p.hasUnsupportedScript=true;assert(!validLocalProcAttributes(p));p.hasUnsupportedScript=false;
    p.attributesMask=LocalProcReduceAbove60;
    for(auto [level,expected]:{std::pair{60,6000},std::pair{61,5800},std::pair{75,3000},std::pair{80,2000},std::pair{90,0},std::pair{100,0}}){
        e.actorLevel=level;assert(localProcChanceBasisPoints(p,e,{true,2000})==unsigned(expected));
    }
    e.actorLevel=80;p.chance=100;
    // Reduction occurs after chance modifiers and before final probability cap.
    assert(localProcChanceBasisPoints(p,e,{true,2000},{0,2,0,1})==6667);
    assert(localProcChanceBasisPoints(p,e,{false,0},{0,2,0,1})==3333);
    p.attributesMask=0;p.ppm=3;e.kind=LocalCombatEventKind::PlayerMelee;
    assert(localProcChanceBasisPoints(p,e,{true,2000},{10,2,1,2})==5667);
    e.kind=LocalCombatEventKind::SpellCast;assert(localProcChanceBasisPoints(p,e,{true,2000},{0,.5f,100,100})==5000);
    p.ppm=0;p.attributesMask=0;p.charges=2;p.cooldownMs=100;
    LocalStatAura aura;aura.remainingMs=1000;aura.stacks=2;aura.procCharges=2;
    localPrepareProcConsumption(p,e,aura);assert(aura.procCharges==1&&aura.remainingMs==1000&&aura.procCooldownMs==100);
    localFinalizeProcConsumption(p,aura);assert(aura.remainingMs==1000);
    localPrepareProcConsumption(p,e,aura);assert(!aura.procCharges&&aura.remainingMs==1000);
    localFinalizeProcConsumption(p,aura);assert(!aura.remainingMs);
    aura.remainingMs=1000;aura.procCharges=2;e.sourceDoNotConsumeResources=true;
    localPrepareProcConsumption(p,e,aura);assert(aura.procCharges==2);localFinalizeProcConsumption(p,aura);assert(aura.remainingMs==1000);
    p.attributesMask=LocalProcUseStacksForCharges;
    localPrepareProcConsumption(p,e,aura);assert(aura.stacks==2&&aura.procCharges==2);
    localFinalizeProcConsumption(p,aura);assert(aura.stacks==1&&aura.remainingMs==1000);
    localFinalizeProcConsumption(p,aura);assert(!aura.stacks&&!aura.remainingMs);
    p=LocalProcDefinition{};p.flags=4;p.chance=100;e=LocalCombatEvent{};e.source=1;e.target=2;e.attempted=e.effective=10;
    p.hitMask=0;assert(localProcMatches(p,e,1));
    e.attempted=e.absorbed=10;e.effective=0;assert(localProcMatches(p,e,1));
    p.flags=8;assert(!localProcMatches(p,e,2)); // taken default excludes pure absorb
    p.hitMask=LocalProcHitAbsorb;assert(localProcMatches(p,e,2));
    e.absorbed=0;e.attempted=e.effective=10;p.hitMask=0;
    p.phaseMask=0;assert(localProcMatches(p,e,2)); // incoming ignores phase
    p.phaseMask=LocalProcPhaseHit;
    e.kind=LocalCombatEventKind::PlayerRanged;e.attackType=LocalCombatAttackType::Unspecified;
    assert(localProcEventFlags(e,1)==0x40&&localProcEventFlags(e,2)==0x100080);
    p.flags=0x40;assert(localProcMatches(p,e,1));
    e.kind=LocalCombatEventKind::SpellCast;e.sourceRangedAuto=true;e.attackType=LocalCombatAttackType::Ranged;
    p.phaseMask=LocalProcPhaseCast;assert(localProcEventFlags(e,1)==0x40&&localProcMatches(p,e,1));
    e.kind=LocalCombatEventKind::SpellFinish;p.phaseMask=LocalProcPhaseFinish;
    assert(localProcEventFlags(e,1)==0x40&&localProcMatches(p,e,1));
    e.kind=LocalCombatEventKind::PlayerRanged;e.sourceRangedAuto=false;p.phaseMask=LocalProcPhaseHit;
    e.procDepth=1;assert(localProcMatches(p,e,1)); // auto attack triggered exception
    e.kind=LocalCombatEventKind::SpellDamage;e.attackType=LocalCombatAttackType::Magic;p.flags=0x10000;
    assert(!localProcMatches(p,e,1));p.attributesMask=LocalProcTriggeredCanProc;assert(localProcMatches(p,e,1));
    p.attributesMask=0;p.flags=2;p.phaseMask=0;p.triggerSchoolMask=64;p.triggerSpellFamily=7;p.hitMask=LocalProcHitCritical;
    e.kind=LocalCombatEventKind::Kill;e.spell=0;e.effective=e.attempted=0;
    assert(localProcMatches(p,e,1));p.flags=1;assert(localProcMatches(p,e,2));
    e.kind=LocalCombatEventKind::Death;e.source=2;e.target=0;p.flags=0x1000000;
    assert(localProcMatches(p,e,2)&&!localProcMatches(p,e,1));
    LocalSpellDefinition source;source.clientSpell=true;source.mana=10;source.sourceDoNotConsumeResources=true;
    e.attackType=LocalCombatAttackType::Ranged;localHydrateProcEventMetadata(e,&source);
    assert(e.sourceHasManaCost&&e.sourceDoNotConsumeResources&&e.attackType==LocalCombatAttackType::Ranged);
    std::cout<<"PASS proc attributes: source predicates, exact provenance, disable masks, level scaling, modifiers, lifecycle, resource phases\n";
}
