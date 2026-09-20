#include "game/local_pet.hpp"
#include "game/local_proc_rules.hpp"
#include <cassert>
#include <iostream>

using namespace wowee::game;

namespace {
LocalRealmPet basePet() {
    LocalRealmPet pet;
    pet.guid=kLocalPetGuidPrefix|1;pet.ownerGuid=1;pet.entry=30;pet.summonSpellId=2;
    pet.kind=LocalPetKind::Controlled;pet.level=20;pet.health=pet.maxHealth=400;
    pet.resourceType=2;pet.power=pet.maxPower=kLocalPetMaxFocus;pet.attackPeriodMs=2000;
    return pet;
}
LocalSpellDefinition summonSpell() {
    LocalSpellDefinition d;d.id=2;d.clientSpell=true;d.summonPetEntry=30;
    d.summonPetKind=uint8_t(LocalPetKind::Controlled);d.summonPetEffectSlot=0;
    return d;
}
LocalSpellDefinition throatTalent() {
    LocalSpellDefinition d;d.id=34950;d.clientSpell=true;d.passive=true;d.talentId=1683;d.talentRank=1;
    d.allowableClasses=1u<<2;d.buffSelfOnly=true;d.schoolMask=1;
    auto& p=d.proc;p.effect=LocalProcEffect::RestorePetPower;p.spellId=34953;p.flags=0x140;
    p.chance=100;p.amount=25;p.resourceType=2;p.recipient=uint8_t(LocalProcRecipient::OwnedPet);
    p.schoolMask=1;p.hitMask=LocalProcHitCritical;p.spellTypeMask=7;p.phaseMask=LocalProcPhaseHit;
    return d;
}
}

int main() {
    // --- Actor validity ----------------------------------------------------
    assert(validLocalPet(basePet()));
    {
        auto bad=basePet();bad.guid=7;assert(!localPetGuid(bad.guid)&&!validLocalPet(bad));
        bad=basePet();bad.ownerGuid=0;assert(!validLocalPet(bad));
        bad=basePet();bad.ownerGuid=bad.guid;assert(!validLocalPet(bad));
        bad=basePet();bad.kind=LocalPetKind::None;assert(!validLocalPet(bad));
        bad=basePet();bad.entry=0;assert(!validLocalPet(bad));
        bad=basePet();bad.summonSpellId=0;assert(!validLocalPet(bad));
        bad=basePet();bad.level=81;assert(!validLocalPet(bad));
        bad=basePet();bad.health=bad.maxHealth+1;assert(!validLocalPet(bad));
        bad=basePet();bad.maxHealth=0;assert(!validLocalPet(bad));
        bad=basePet();bad.resourceType=3;assert(!validLocalPet(bad));
        bad=basePet();bad.power=bad.maxPower+1;assert(!validLocalPet(bad));
        // A summon with no bar must carry no resource at all.
        bad=basePet();bad.resourceType=255;assert(!validLocalPet(bad));
        bad=basePet();bad.resourceType=255;bad.power=bad.maxPower=0;assert(validLocalPet(bad));
        bad=basePet();bad.powerRegenElapsedMs=kLocalPetFocusRegenIntervalMs+1;assert(!validLocalPet(bad));
        bad=basePet();bad.attackPeriodMs=100;assert(!validLocalPet(bad));
        bad=basePet();bad.attackPeriodMs=20000;assert(!validLocalPet(bad));
        // A controlled summon is indefinite; a guardian must state a lifetime.
        bad=basePet();bad.remainingMs=1000;assert(!validLocalPet(bad));
        bad=basePet();bad.kind=LocalPetKind::Guardian;assert(!validLocalPet(bad));
        bad=basePet();bad.kind=LocalPetKind::Guardian;bad.remainingMs=45000;assert(validLocalPet(bad));
        bad=basePet();bad.kind=LocalPetKind::Guardian;bad.remainingMs=3600001;assert(!validLocalPet(bad));
        bad=basePet();bad.x=std::numeric_limits<float>::quiet_NaN();assert(!validLocalPet(bad));
        bad=basePet();bad.attackTimer=-1;assert(!validLocalPet(bad));
        bad=basePet();bad.name=std::string(97,'a');assert(!validLocalPet(bad));
    }
    // --- Roster bounds -----------------------------------------------------
    {
        std::vector<LocalRealmPet> roster;
        for(size_t i=0;i<kLocalMaxPets;++i) {
            auto guardian=basePet();guardian.guid=kLocalPetGuidPrefix|(i+1);
            guardian.ownerGuid=i+1;roster.push_back(guardian);
        }
        assert(validLocalPets(roster));
        auto tooMany=roster;tooMany.push_back(basePet());tooMany.back().guid=kLocalPetGuidPrefix|99;
        tooMany.back().ownerGuid=99;assert(!validLocalPets(tooMany));
        auto duplicate=roster;duplicate[1].guid=duplicate[0].guid;assert(!validLocalPets(duplicate));
        // One controlled summon per owner; guardians stay separately bounded.
        auto twoControlled=roster;twoControlled[1].ownerGuid=twoControlled[0].ownerGuid;
        assert(!validLocalPets(twoControlled));
        std::vector<LocalRealmPet> guardians;
        for(size_t i=0;i<=kLocalMaxGuardiansPerOwner;++i) {
            auto guardian=basePet();guardian.guid=kLocalPetGuidPrefix|(100+i);
            guardian.kind=LocalPetKind::Guardian;guardian.remainingMs=45000;guardians.push_back(guardian);
        }
        assert(!validLocalPets(guardians));
        guardians.pop_back();assert(validLocalPets(guardians));
    }
    // --- Focus regeneration -------------------------------------------------
    {
        auto pet=basePet();pet.power=0;
        assert(localPetRegenerate(pet,kLocalPetFocusRegenIntervalMs-1)==0&&pet.power==0);
        assert(localPetRegenerate(pet,1)==kLocalPetFocusRegenAmount&&pet.power==kLocalPetFocusRegenAmount);
        // The interval does not accumulate more than one tick of credit.
        pet.power=0;pet.powerRegenElapsedMs=0;
        assert(localPetRegenerate(pet,60000)==kLocalPetFocusRegenAmount);
        pet.power=kLocalPetMaxFocus-1;pet.powerRegenElapsedMs=0;
        assert(localPetRegenerate(pet,kLocalPetFocusRegenIntervalMs)==1&&pet.power==kLocalPetMaxFocus);
        assert(localPetRegenerate(pet,kLocalPetFocusRegenIntervalMs)==0);
        // No bar, no regeneration; a dead summon regenerates nothing.
        auto bare=basePet();bare.resourceType=255;bare.power=bare.maxPower=0;
        assert(localPetRegenerate(bare,kLocalPetFocusRegenIntervalMs)==0);
        auto dead=basePet();dead.dead=true;dead.power=0;
        assert(localPetRegenerate(dead,kLocalPetFocusRegenIntervalMs)==0&&dead.power==0);
    }
    // --- Recipient energize -------------------------------------------------
    {
        auto pet=basePet();pet.power=10;
        assert(localPetRestorePower(pet,2,25)==25&&pet.power==35);
        assert(localPetRestorePower(pet,1,25)==0&&pet.power==35); // Wrong power system.
        assert(localPetRestorePower(pet,2,0)==0&&pet.power==35);
        pet.power=kLocalPetMaxFocus-5;
        assert(localPetRestorePower(pet,2,25)==5&&pet.power==kLocalPetMaxFocus);
        assert(localPetRestorePower(pet,2,25)==0);
        pet.dead=true;pet.power=0;assert(localPetRestorePower(pet,2,25)==0&&pet.power==0);
    }
    // --- Owner resolution ---------------------------------------------------
    {
        LocalRealmPlayer owner;owner.guid=1;owner.mapId=0;owner.instanceId=0;owner.health=owner.maxHealth=100;
        LocalRealmPlayer other;other.guid=2;other.health=other.maxHealth=100;
        std::vector<LocalRealmPlayer*> roster{&owner,&other};
        auto pet=basePet();
        assert(localPetOwner(pet,roster)==&owner);
        assert(localPetOwnerEligible(pet,localPetOwner(pet,roster)));
        // Owner and actor are not interchangeable: a different instance, a
        // death, a flight or an absent owner all retire the summon.
        pet.instanceId=3;assert(localPetOwner(pet,roster)==nullptr);
        pet.instanceId=0;owner.dead=true;assert(!localPetOwnerEligible(pet,&owner));
        owner.dead=false;owner.health=0;assert(!localPetOwnerEligible(pet,&owner));
        owner.health=100;owner.flight.active=true;assert(!localPetOwnerEligible(pet,&owner));
        owner.flight.active=false;assert(localPetOwnerEligible(pet,&owner));
        assert(!localPetOwnerEligible(pet,nullptr));
    }
    // --- Summon source profile ---------------------------------------------
    {
        assert(validLocalSummonPet(summonSpell()));
        LocalSpellDefinition none;assert(validLocalSummonPet(none)); // Not a summon at all.
        auto bad=summonSpell();bad.summonPetKind=uint8_t(LocalPetKind::Guardian);assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.summonPetEffectSlot=3;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.summonPetDurationMs=1000;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.clientSpell=false;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.passive=true;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.triggeredOnly=true;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.damage=1;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.heal=1;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.buffHealth=1;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.formId=1;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.wardProfile=1;assert(!validLocalSummonPet(bad));
        bad=summonSpell();bad.proc.effect=LocalProcEffect::HealOwner;assert(!validLocalSummonPet(bad));
    }
    // --- Owned-creature energize proc metadata ------------------------------
    {
        const auto talent=throatTalent();
        assert(validLocalProc(talent));
        // Recipient and effect are one pair: neither may appear without the other.
        auto bad=talent;bad.proc.recipient=uint8_t(LocalProcRecipient::AuraOwner);assert(!validLocalProc(bad));
        bad=talent;bad.proc.recipient=7;assert(!validLocalProc(bad));
        LocalSpellDefinition ownerEffect;ownerEffect.id=100;ownerEffect.passive=true;ownerEffect.talentId=5;
        ownerEffect.proc.effect=LocalProcEffect::RestorePower;ownerEffect.proc.spellId=101;
        ownerEffect.proc.flags=4;ownerEffect.proc.chance=100;ownerEffect.proc.amount=5;
        ownerEffect.proc.resourceType=1;ownerEffect.proc.schoolMask=1;
        assert(validLocalProc(ownerEffect));
        ownerEffect.proc.recipient=uint8_t(LocalProcRecipient::OwnedPet);
        assert(!validLocalProc(ownerEffect));
        // The energize is a learned passive talent with a source power system,
        // a distinct child spell and no aura, charges or cooldown of its own.
        bad=talent;bad.passive=false;assert(!validLocalProc(bad));
        bad=talent;bad.talentId=0;assert(!validLocalProc(bad));
        bad=talent;bad.durationMs=1000;assert(!validLocalProc(bad));
        bad=talent;bad.triggeredOnly=true;assert(!validLocalProc(bad));
        bad=talent;bad.proc.resourceType=1;assert(!validLocalProc(bad));
        bad=talent;bad.proc.resourceType=0;assert(!validLocalProc(bad));
        bad=talent;bad.proc.spellId=bad.id;assert(!validLocalProc(bad));
        bad=talent;bad.proc.spellId=0;assert(!validLocalProc(bad));
        bad=talent;bad.proc.flags=0;assert(!validLocalProc(bad));
        bad=talent;bad.proc.amount=0;assert(!validLocalProc(bad));
        bad=talent;bad.proc.amount=1001;assert(!validLocalProc(bad));
        bad=talent;bad.proc.charges=1;assert(!validLocalProc(bad));
        bad=talent;bad.proc.cooldownMs=1000;assert(!validLocalProc(bad));
        bad=talent;bad.proc.ppm=1;assert(!validLocalProc(bad));
        bad=talent;bad.proc.allowTriggered=true;assert(!validLocalProc(bad));
        bad=talent;bad.proc.requiredForms=1;assert(!validLocalProc(bad));
        bad=talent;bad.proc.phaseMask=LocalProcPhaseCast;assert(!validLocalProc(bad));
        bad=talent;bad.proc.spellFamily=9;assert(!validLocalProc(bad));
        bad=talent;bad.buffAbsorb=1;assert(!validLocalProc(bad));
        bad=talent;bad.procParentTalentId=1;assert(!validLocalProc(bad));
    }
    // --- Owned-creature swings in the shared event vocabulary ---------------
    {
        const uint64_t petGuid=kLocalPetGuidPrefix|1,enemy=10;
        LocalCombatEvent swing{0,petGuid,enemy,0,0,0,50,50,0,LocalCombatEventKind::PetMelee};
        swing.schoolMask=1;swing.attackType=LocalCombatAttackType::Melee;swing.weaponPeriodMs=2000;
        // A summon's swing is a melee auto attack done by the summon and taken
        // by its victim. Its owner is neither, so it selects nothing.
        assert(localProcEventFlags(swing,petGuid)==0x400004u);
        assert(localProcEventFlags(swing,enemy)==0x100008u);
        assert(localProcEventFlags(swing,1)==0);
        assert(localProcEventHitMask(swing)==LocalProcHitNormal);
        assert(localProcEventSpellTypeMask(swing)==1);
        auto critical=swing;critical.outcome=LocalMeleeOutcome::Critical;
        assert(localProcEventHitMask(critical)==LocalProcHitCritical);
        auto missed=swing;missed.outcome=LocalMeleeOutcome::Miss;missed.attempted=missed.effective=0;
        assert(localProcEventHitMask(missed)==LocalProcHitMiss);
        auto blocked=swing;blocked.attempted=20;blocked.blocked=20;blocked.effective=0;
        assert(localProcEventHitMask(blocked)&LocalProcHitFullBlock);
        LocalCombatEvent hydrated{0,petGuid,enemy,0,0,0,10,10,0,LocalCombatEventKind::PetMelee};
        localHydrateProcEventMetadata(hydrated,nullptr);
        assert(hydrated.attackType==LocalCombatAttackType::Melee);
    }
    std::cout<<"PASS P03/D1 owned-creature source rules: actor validity, roster bounds, source focus regeneration, "
               "recipient energize, owner resolution kept distinct from the actor, reviewed summon profile, "
               "energize proc metadata and owned-creature swings in the shared event vocabulary\n";
    return 0;
}
