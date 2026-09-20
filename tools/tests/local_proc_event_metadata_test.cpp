#include "game/local_proc_rules.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
int main() {
    LocalSpellDefinition d;d.clientSpell=true;
    for(uint8_t value=0;value<4;++value) {
        LocalCombatEvent e;e.kind=LocalCombatEventKind::SpellDamage;e.spell=1495;d.sourceDamageClass=value;
        localHydrateProcEventMetadata(e,&d);assert(e.attackType==localProcSpellAttackType(value));
    }
    LocalCombatEvent e;e.kind=LocalCombatEventKind::PlayerMelee;
    localHydrateProcEventMetadata(e,&d);assert(e.attackType==LocalCombatAttackType::Melee);
    e.attackType=LocalCombatAttackType::Ranged;localHydrateProcEventMetadata(e,&d);
    assert(e.attackType==LocalCombatAttackType::Ranged);
    d.sourceNotAProc=true;localHydrateProcEventMetadata(e,&d);
    assert(e.sourceNotAProc&&e.attackType==LocalCombatAttackType::Ranged);
    d.sourceNotAProc=false;localHydrateProcEventMetadata(e,&d);assert(!e.sourceNotAProc);
    e={};e.kind=LocalCombatEventKind::ProcHeal;d.clientSpell=false;
    d.sourceNotAProc=true;
    localHydrateProcEventMetadata(e,&d);assert(e.attackType==LocalCombatAttackType::Unspecified);
    assert(!e.sourceNotAProc);
    localHydrateProcEventMetadata(e,nullptr);assert(e.attackType==LocalCombatAttackType::Unspecified);
    e.spell=1;e.kind=LocalCombatEventKind::SpellCast;e.spellTypeMask=1;
    assert(localProcEventSpellTypeMask(e)==7);
    e.kind=LocalCombatEventKind::SpellFinish;assert(localProcEventSpellTypeMask(e)==7);
    e.kind=LocalCombatEventKind::DirectHeal;e.attempted=100;e.effective=0;
    assert(localProcEventSpellTypeMask(e)==2); // Full overheal still has HealInfo.
    e.absorbed=100;assert(localProcEventSpellTypeMask(e)==4);
    e.kind=LocalCombatEventKind::SpellDamage;assert(localProcEventSpellTypeMask(e)==1);
    e.absorbed=0;e.blocked=100;assert(localProcEventSpellTypeMask(e)==4);
    e.blocked=0;e.outcome=LocalMeleeOutcome::Dodge;assert(localProcEventSpellTypeMask(e)==4);
    e.outcome=LocalMeleeOutcome::Hit;assert(localProcEventSpellTypeMask(e)==1);
    e.attempted=0;assert(localProcEventSpellTypeMask(e)==4);
    e.kind=LocalCombatEventKind::SpellHit;e.spellTypeMask=1;assert(localProcEventSpellTypeMask(e)==4);
    assert(!(LocalProcSpellEventFlags&0xcu));assert((LocalProcSpellEventFlags&0xc0u)==0xc0u);
    assert(!(LocalProcSpellEventFlags&0x100000u));assert((LocalProcSpellEventFlags&0xc0000u)==0xc0000u);
    e={};e.kind=LocalCombatEventKind::NpcMelee;e.attempted=e.blocked=100;e.outcome=LocalMeleeOutcome::Block;
    assert(localProcEventHitMask(e)==(LocalProcHitBlock|LocalProcHitFullBlock));
    e.kind=LocalCombatEventKind::SpellDamage;e.spell=1;e.attackType=LocalCombatAttackType::Melee;
    assert(!(localProcEventHitMask(e)&LocalProcHitFullBlock));
    e.kind=LocalCombatEventKind::NpcMelee;e.blocked=50;e.absorbed=50;
    assert(localProcEventHitMask(e)==(LocalProcHitBlock|LocalProcHitAbsorb));
    LocalProcDefinition p;p.flags=4;p.chance=100;p.triggerSpellFamily=11;p.triggerSpellFamilyFlags={1,0,0};
    e={};e.source=10;e.target=20;e.attempted=e.effective=40;e.schoolMask=1;
    e.kind=LocalCombatEventKind::PlayerMelee;e.attackType=LocalCombatAttackType::Melee;
    assert(localProcMatches(p,e,10)); // Family does not filter ordinary swings.
    e.kind=LocalCombatEventKind::SpellDamage;e.spell=1495;p.flags=16;
    assert(!localProcMatches(p,e,10));
    e.spellFamily=11;e.spellFamilyFlags={1,0,0};assert(localProcMatches(p,e,10));
    p.phaseMask=LocalProcPhaseCast;assert(!localProcMatches(p,e,10));
    p.flags=32;assert(localProcMatches(p,e,20)); // Incoming hit ignores spell phase.
    p.flags=16;p.phaseMask=LocalProcPhaseHit;e.procDepth=1;
    assert(!localProcMatches(p,e,10));
    d.clientSpell=true;d.sourceDamageClass=2;d.sourceNotAProc=true;
    localHydrateProcEventMetadata(e,&d);assert(localProcMatches(p,e,10));
    d.sourceNotAProc=false;localHydrateProcEventMetadata(e,&d);assert(!localProcMatches(p,e,10));
    e.kind=LocalCombatEventKind::PlayerMelee;e.spell=0;p.flags=4;
    assert(localProcMatches(p,e,10)); // Generic triggered auto exception.
    std::cout<<"PASS source proc event metadata: all four damage classes, explicit/auto/synthetic identity, cast all types, overheal, absorbed damage, blocked/avoided/no-damage spell type, conditional filter categories\n";
}
