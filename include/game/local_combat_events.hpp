#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace wowee::game {
// Appended only, never reordered: these values are persisted and LAN-encoded.
// Evade and Interrupt are deliberately absent - see the source audit.
enum class LocalMeleeOutcome : uint8_t { Hit, Miss, Dodge, Parry, Block, Critical, Glancing, Crushing, Reflect,
    Resist, Immune, Deflect };
struct LocalMeleeView {
    uint32_t serial=0,spell=0,amount=0,blocked=0;
    uint64_t source=0,target=0;
    LocalMeleeOutcome outcome=LocalMeleeOutcome::Hit;
    bool offHand=false;
    bool healing=false; // Direct heal observation; outcome carries critical.
    // P04: the partial-resist amount of Unit::CalcAbsorbResist (Unit.cpp:2341-2396),
    // the reference's SMSG_SPELLNONMELEEDAMAGELOG.resist. Appended last so every
    // earlier offset of the LAN 83 view keeps its meaning; LAN 84 carries it. A
    // full resist is outcome Resist with `amount` 0 and `resisted` the whole hit.
    uint32_t resisted=0;
};
enum class LocalCombatEventKind : uint8_t {
    PlayerMelee, NpcMelee, SpellDamage, PeriodicDamage, DirectHeal, PeriodicHeal,
    ProcDamage, ProcMana, ProcHeal, SpellCast, ProcPower, ProcCombo, ProcAura, SpellHit, SpellFinish,
    PlayerRanged, Kill, Death,
    // An owned creature's own auto attack. The summon is the source; its owner
    // is neither source nor target, exactly as Unit::AttackerStateUpdate leaves
    // them. Appended so no existing observation changes value.
    PetMelee
};
enum class LocalCombatAttackType : uint8_t { Unspecified, Melee, Ranged, Magic, None };
// Proc hit bits mirror the 3.3.5 server proc-extra result bits.
enum LocalProcHitMask : uint32_t {
    LocalProcHitNormal=0x1, LocalProcHitCritical=0x2, LocalProcHitMiss=0x4,
    LocalProcHitFullResist=0x8,
    LocalProcHitDodge=0x10, LocalProcHitParry=0x20, LocalProcHitBlock=0x40,
    LocalProcHitImmune=0x100, LocalProcHitDeflect=0x200,
    LocalProcHitReflect=0x800, LocalProcHitAbsorb=0x400, LocalProcHitFullBlock=0x2000
};
// PROC_HIT_EVADE 0x80 and PROC_HIT_INTERRUPT 0x1000 stay out: evade needs an AI
// evade/reachability state this realm does not have, and interrupt is not even a
// member of the reference's own PROC_HIT_MASK_ALL.
constexpr uint32_t LocalProcSupportedHits=LocalProcHitNormal|LocalProcHitCritical|LocalProcHitMiss|
    LocalProcHitFullResist|LocalProcHitDodge|LocalProcHitParry|LocalProcHitBlock|LocalProcHitImmune|
    LocalProcHitDeflect|LocalProcHitAbsorb|LocalProcHitFullBlock|LocalProcHitReflect;
static_assert(LocalProcSupportedHits==0x2F7Fu,"Supported hit bits must match the reviewed source subset");
enum LocalProcPhaseMask : uint8_t { LocalProcPhaseCast=1, LocalProcPhaseHit=2, LocalProcPhaseFinish=4 };
struct LocalCombatEvent {
    uint64_t sequence=0, source=0, target=0;
    uint32_t spell=0, mapId=0, instanceId=0;
    // Damage attempted is after armor but before absorption; healing attempted
    // includes overheal. Effective is the actual health delta, excluding overkill;
    // ProcMana/ProcPower/ProcCombo instead report their resource delta.
    uint32_t attempted=0, effective=0, absorbed=0;
    LocalCombatEventKind kind=LocalCombatEventKind::PlayerMelee;
    bool killed=false;
    uint32_t auraSpell=0;
    LocalMeleeOutcome outcome=LocalMeleeOutcome::Hit;
    uint32_t blocked=0;bool offHand=false;
    // Appended metadata preserves the aggregate prefix used by existing
    // producers. Identity describes the triggering event, not the proc child.
    uint32_t schoolMask=0,spellFamily=0;
    std::array<uint32_t,3> spellFamilyFlags{};
    uint32_t appliedCostAuraSpell=0;
    uint64_t appliedCostAuraGeneration=0;
    uint32_t sourceRawCastTimeMs=0;
    bool omenProcEligible=false;
    uint32_t weaponPeriodMs=0; // Base weapon period, never frame delta or haste.
    LocalCombatAttackType attackType=LocalCombatAttackType::Unspecified;
    bool positiveSpell=false; // Required for the SpellCast phase.
    uint8_t spellTypeMask=0; // Hit classification: damage1, heal2, other4; CAST/FINISH use all7.
    uint64_t parentSequence=0,rootSequence=0,auraCasterGuid=0,auraOwnerGuid=0;
    uint8_t procDepth=0;
    // Aura observations never enter damage/healing proc selection. An
    // application includes refresh; false records charge consumption.
    uint32_t auraDurationMs=0;
    uint8_t auraCharges=0;
    bool auraApplied=false;
    bool sourceNotAProc=false;
    bool sourceHasManaCost=false,sourceItemCast=false,sourceDoNotConsumeResources=false;
    bool actorIsPlayer=false,actionTargetKnown=false,actionTargetHonorOrXpEligible=false;
    uint8_t actorLevel=0;
    bool sourceRangedAuto=false;
    uint64_t reflectionSource=0;
    bool reflectionOnly=false;
    // P04: damage taken by the target's school resistance before absorption
    // (DamageInfo::ResistDamage, Unit.cpp:270-275). Every damage event keeps
    // resisted + effective + absorbed + blocked == attempted whenever nothing
    // was lost to overkill; a kill is the one event where effective is the
    // health that was actually there. Zero on every event before the reference.
    uint32_t resisted=0;
};

// Bounded authority-side observation, not a reliable delivery queue. The
// proc dispatcher consumes events synchronously, never replays this history.
// No guest command can submit an event. No allocations occur while recording.
class LocalCombatHistory {
public:
    static constexpr size_t Capacity=128;
    uint64_t record(LocalCombatEvent event) {
        event.sequence=++sequence_;
        if(!event.sequence)event.sequence=++sequence_;
        if(!event.rootSequence)event.rootSequence=event.sequence;
        rows_[next_]=event;next_=(next_+1)%Capacity;
        if(count_<Capacity)++count_;else ++overwritten_;
        return event.sequence;
    }
    std::vector<LocalCombatEvent> snapshot() const {
        std::vector<LocalCombatEvent> out;out.reserve(count_);
        const auto first=(next_+Capacity-count_)%Capacity;
        for(size_t i=0;i<count_;++i)out.push_back(rows_[(first+i)%Capacity]);
        return out;
    }
    uint64_t overwritten()const{return overwritten_;}
    void clear(){next_=count_=0;sequence_=overwritten_=0;}
private:
    std::array<LocalCombatEvent,Capacity> rows_{};
    size_t next_=0,count_=0;
    uint64_t sequence_=0,overwritten_=0;
};
}
