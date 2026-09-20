#include "game/local_proc_rules.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace wowee::game;
int main() {
    LocalProcDefinition p;p.flags=0x8;p.chance=100;
    LocalCombatEvent e;e.source=10;e.target=20;e.attempted=e.effective=40;e.kind=LocalCombatEventKind::NpcMelee;e.schoolMask=1;
    assert(localProcMatches(p,e,20));assert(!localProcMatches(p,e,10));assert(!localProcMatches(p,e,99));
    p.flags=0x4;assert(localProcMatches(p,e,10));assert(!localProcMatches(p,e,20));
    p.triggerSchoolMask=8;assert(!localProcMatches(p,e,10));e.schoolMask=8;assert(localProcMatches(p,e,10));
    e.kind=LocalCombatEventKind::SpellDamage;e.spell=1;e.attackType=LocalCombatAttackType::Melee;p.flags=0x10;
    p.triggerSpellFamily=11;p.triggerSpellFamilyFlags[2]=0x80000000;e.spellFamily=11;e.spellFamilyFlags[2]=0x80000000;
    assert(localProcMatches(p,e,10));e.spellFamily=8;assert(!localProcMatches(p,e,10));
    e.spellFamily=11;e.spellFamilyFlags={0x80000000,0,0};assert(!localProcMatches(p,e,10));
    p.triggerSpellFamily=0;p.triggerSpellFamilyFlags={};p.triggerSchoolMask=0;e.kind=LocalCombatEventKind::NpcMelee;p.flags=4;
    p.hitMask=LocalProcHitCritical;assert(!localProcMatches(p,e,10));e.outcome=LocalMeleeOutcome::Critical;assert(localProcMatches(p,e,10));
    e.outcome=LocalMeleeOutcome::Miss;e.effective=e.attempted=0;assert(!localProcMatches(p,e,10));p.hitMask=LocalProcHitMiss;assert(localProcMatches(p,e,10));
    e.outcome=LocalMeleeOutcome::Hit;e.attempted=e.absorbed=40;p.hitMask=LocalProcHitNormal;assert(!localProcMatches(p,e,10));
    p.hitMask=LocalProcHitAbsorb;assert(localProcMatches(p,e,10));e.effective=1;e.absorbed=39;assert(localProcEventHitMask(e)&LocalProcHitNormal);
    e.procDepth=1;assert(localProcMatches(p,e,10));e.kind=LocalCombatEventKind::SpellDamage;p.flags=0x10;assert(!localProcMatches(p,e,10));e.sourceNotAProc=true;assert(localProcMatches(p,e,10));e.sourceNotAProc=false;p.allowTriggered=true;assert(localProcMatches(p,e,10));
    e.procDepth=0;e.absorbed=0;e.effective=e.attempted=40;p.hitMask=LocalProcHitNormal;
    p.flags=0x800000;e.offHand=false;assert(!localProcMatches(p,e,10));e.offHand=true;assert(localProcMatches(p,e,10));
    p.flags=0x80000;e.kind=LocalCombatEventKind::PeriodicHeal;p.spellTypeMask=1;assert(!localProcMatches(p,e,20));
    e.kind=LocalCombatEventKind::PeriodicDamage;assert(localProcMatches(p,e,20));
    assert(!(localProcEventFlags(e,10)&0xc00000));
    e.kind=LocalCombatEventKind::SpellCast;e.attackType=LocalCombatAttackType::Magic;e.spellTypeMask=1;p.flags=0x10000;
    assert(!localProcMatches(p,e,10));p.phaseMask=LocalProcPhaseCast;assert(localProcMatches(p,e,10));
    p.flags=0x20000;assert(!localProcMatches(p,e,20));
    e.kind=LocalCombatEventKind::PlayerMelee;e.attackType=LocalCombatAttackType::Melee;e.weaponPeriodMs=2000;
    p.ppm=3;p.chance=100;assert(localProcChanceBasisPoints(p,e)==1000);
    p.phaseMask=LocalProcPhaseHit;p.flags=0x8;assert(localProcMatches(p,e,20));
    e.weaponPeriodMs=3000;assert(localProcChanceBasisPoints(p,e)==1500);
    e.weaponPeriodMs=0;assert(!localProcChanceBasisPoints(p,e));e.weaponPeriodMs=2000;e.kind=LocalCombatEventKind::PeriodicDamage;assert(!localProcChanceBasisPoints(p,e));
    e.kind=LocalCombatEventKind::PlayerMelee;p.ppm=std::numeric_limits<float>::quiet_NaN();assert(!validLocalProcFilters(p));
    p.ppm=0;p.chance=33;assert(localProcChanceBasisPoints(p,e)==3300);
    uint64_t seed=123;assert(!localRollProcBasisPoints(0,seed)&&seed==123);assert(localRollProcBasisPoints(10000,seed)&&seed==123);
    uint64_t replay=seed;for(int i=0;i<32;++i)assert(localRollProcBasisPoints(3300,seed)==localRollProcBasisPoints(3300,replay));
    LocalSpellDefinition d;assert(validLocalProc(d));d.proc.triggerSchoolMask=1;assert(!validLocalProc(d));d.proc={};
    d.passive=true;d.talentId=1;d.proc.effect=LocalProcEffect::RestorePower;d.proc.spellId=2;d.proc.flags=4;d.proc.ppm=3;d.proc.amount=1;d.proc.resourceType=1;
    assert(validLocalProc(d));assert(!localHasTimedAura(d));d.proc.charges=1;assert(!validLocalProc(d));d.proc.charges=0;d.proc.resourceType=5;assert(!validLocalProc(d));
    LocalCombatHistory history;assert(history.record(e)==1);assert(history.snapshot()[0].rootSequence==1);
    for(size_t i=0;i<LocalCombatHistory::Capacity;++i)history.record(e);
    assert(history.snapshot().size()==LocalCombatHistory::Capacity&&history.overwritten()==1);
    std::cout << "PASS P03 proc matching: owner/recipient, school, family96, hit/crit/miss/absorb, offhand, periodic damage/heal filter, cast/hit phases, triggered opt-in, PPM base-period conversion, finite bounds, deterministic rolls, strict no-proc metadata, passive resource validation, history root sequence\n";
}
