#pragma once
#include "game/local_gameplay.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// P03/D1: owned creatures as real combat-event producers and recipients.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   Unit.cpp::GetSpellModOwner            - a pet, guardian or totem resolves
//                                           spell modifiers through its owning
//                                           player; the owner is NOT the caster.
//   Unit.cpp::GetCharmerOrOwnerPlayerOrPlayerItself and Unit::Kill - reward,
//                                           experience and quest credit follow
//                                           the owner while the killing blow,
//                                           threat and proc events stay with
//                                           the creature that dealt them.
//   Pet.cpp::Regenerate                   - POWER_FOCUS adds 24 per regen tick.
//   Pet.h::PET_FOCUS_REGEN_INTERVAL       - that tick is 4 * IN_MILLISECONDS.
//   Pet.cpp::InitStatsForLevel            - a focus pet has a fixed pool of 100
//                                           and takes its owner's level.
//   SharedDefines.h::SPELL_EFFECT_SUMMON_PET (56) - the only summon effect this
//                                           package admits.
//
// CORRECTION : this comment previously said effect 28 could not be
// admitted because "the supplied client data does not carry" the
// SummonProperties record. It does. SummonProperties.dbc is present, 200
// records x 6 fields, and classifies 2,671 of 2,671 effect-28 slots with none
// unresolved (the source audit sections 0.2 and 1.1). Effect 28 is
// still not admitted here, for a different and real reason: of its 114
// player-castable slots, 98 are SUMMON_TYPE_TOTEM and the four
// SUMMON_CATEGORY_PET rows are finite-duration guardians routed through
// SummonGuardian, so they are P08's lifecycle and their classes' content, not
// this package's command bar (audit section 8.4).
//
// P07  adds commands and stances, per-level pet stat scaling from
// pet_levelstats and generated names; it still does not own stables, taming,
// feeding, autocast or revival, and P14 owns Hunter/Warlock class behaviour.
// See the source audit for what landed and what has zero producers.
namespace wowee::game {

enum class LocalPetKind : uint8_t {
    None = 0,
    /// UNIT_FLAG_PLAYER_CONTROLLED summon the owner steers and keeps.
    Controlled = 1,
    /// A summon with its own duration that the owner does not command.
    Guardian = 2,
};

/// CommandStates, Unit.h:574-577. Exactly four; there is no COMMAND_MOVE_TO at
/// build 12340.
///
/// Only two of the four are ever a *stored* state, and that is the reference's
/// own shape rather than a simplification here. In
/// WorldSession::HandlePetActionHelper (PetHandler.cpp:162-304) only
/// COMMAND_STAY and COMMAND_FOLLOW call `charmInfo->SetCommandState`;
/// COMMAND_ATTACK sets the separate `SetIsCommandAttack(true)` flag and leaves
/// the command state alone, and COMMAND_ABANDON is the dismissal itself - on a
/// SUMMON_PET a death rather than an unsummon. That distinction is load-bearing:
/// PetAI::HandleReturnMovement (PetAI.cpp:561-610) branches on
/// `HasCommandState(COMMAND_STAY)` else COMMAND_FOLLOW, and `_stopAttack`
/// (:72-90) clears IsCommandAttack **without** touching the command state, so a
/// staying pet told to attack returns to its stay point when the victim is
/// gone - not to its owner. `LocalRealmPet::command` is therefore Stay or
/// Follow, and the attack order is `LocalRealmPet::commandAttack`.
enum class LocalPetCommand : uint8_t { Stay = 0, Follow = 1, Attack = 2, Abandon = 3 };
/// ReactStates, Unit.h:567-569.
enum class LocalPetReact : uint8_t { Passive = 0, Defensive = 1, Aggressive = 2 };
/// A fresh summon follows. Spell::EffectSummonPet only overrides the react
/// state when the *caster* is a creature (SpellEffects.cpp:3452-3459); for a
/// player cast it keeps the creature default, and Creature's own constructor
/// initialises m_reactState to REACT_AGGRESSIVE (Creature.cpp:263). Nothing in
/// the SUMMON_PET path changes it - Pet::LoadPetFromDB restores the SAVED one
/// (Pet.cpp:348), which is why the react state is persisted here, and the only
/// explicit override in Pet.cpp is the bloodworm's (:1371). Aggressive is also
/// exactly what this build's pet tick has always done unconditionally.
inline constexpr LocalPetCommand kLocalPetDefaultCommand = LocalPetCommand::Follow;
inline constexpr LocalPetReact kLocalPetDefaultReact = LocalPetReact::Aggressive;

/// Pet.cpp::Regenerate / Pet.h::PET_FOCUS_REGEN_INTERVAL.
inline constexpr uint32_t kLocalPetFocusRegenAmount = 24;
inline constexpr uint32_t kLocalPetFocusRegenIntervalMs = 4000;
/// Pet.cpp::InitStatsForLevel - focus pets carry a fixed pool.
inline constexpr uint32_t kLocalPetMaxFocus = 100;
/// Bounded authority roster. One controlled summon per owner, as the source
/// allows, plus room for short-lived guardians without unbounded growth.
inline constexpr size_t kLocalMaxPets = 8;
inline constexpr size_t kLocalMaxGuardiansPerOwner = 2;
/// The summon appears beside its owner and follows within this distance, and
/// is recalled when it can no longer reach them. Same eight yards every other
/// owner-scoped rule in this realm uses.
inline constexpr float kLocalPetFollowDistance = 8.0f;
inline constexpr float kLocalPetLeashDistance = 60.0f;
inline constexpr float kLocalPetMeleeRange = 4.0f;
/// PetAI::SelectNextTarget's aggressive-only arm passes MAX_AGGRO_RADIUS
/// (Unit.h:44, 45 yards) to Creature::SelectNearestTargetInAttackDistance,
/// which floors it at ATTACK_DISTANCE (ObjectDefines.h:25, 5 yards). 45 is
/// above that floor, so the floor never applies and the search radius is 45.
inline constexpr float kLocalPetAutoAcquireRange = 45.0f;

/// One owned creature. Deliberately separate from LocalRealmNpc: the spawn
/// refresher owns that list and would evict a summon on the next catalog query.
struct LocalRealmPet {
    uint64_t guid = 0, ownerGuid = 0, targetGuid = 0;
    /// Authority-only identity of this summon. A dismissed and re-summoned pet
    /// never receives a callback prepared for its predecessor.
    uint64_t summonEpoch = 0;
    uint32_t entry = 0, displayId = 0, mapId = 0, instanceId = 0;
    uint32_t summonSpellId = 0;
    LocalPetKind kind = LocalPetKind::None;
    uint8_t level = 1;
    uint32_t health = 0, maxHealth = 0;
    /// Focus for a beast, the owner's own system for anything else the source
    /// gives a pool. 255 denotes a summon with no power bar.
    uint8_t resourceType = 255;
    uint32_t power = 0, maxPower = 0, powerRegenElapsedMs = 0;
    uint32_t attackPeriodMs = 2000;
    /// Guardian duration in milliseconds; zero is the indefinite controlled pet.
    uint32_t remainingMs = 0;
    float x = 0, y = 0, z = 0, orientation = 0, attackTimer = 0;
    bool dead = false;
    /// P07 . CharmInfo::GetCommandState and Creature::GetReactState, the
    /// two bytes SMSG_PET_SPELLS sends after the duration
    /// (Player::PetSpellInitialize, Player.cpp:9760-9765). Both are persisted
    /// and replicated, which is why this checkpoint bumps Save and LAN.
    LocalPetCommand command = kLocalPetDefaultCommand;
    LocalPetReact react = kLocalPetDefaultReact;
    /// CharmInfo::IsCommandAttack. Set by COMMAND_ATTACK, cleared by
    /// COMMAND_STAY, COMMAND_FOLLOW and PetAI::_stopAttack. It overrides the
    /// stance (an explicitly commanded pet attacks whatever its stance is) and
    /// it is what lets a staying pet chase the one victim it was pointed at.
    bool commandAttack = false;
    /// P03 : the Imp's learned Firebolt rank shares one persisted
    /// autocast preference. Learning a higher rank must not reset the toggle.
    /// Pet::addSpell maps ACT_DECIDE to ACT_DISABLED for autocastable spells;
    /// a new pet starts disabled. Ignored for creatures without Firebolt.
    bool fireboltAutocast = false;
    /// Where COMMAND_STAY pinned this summon. CharmInfo::SaveStayPosition /
    /// GetStayPosition - a staying pet holds the point it was told to hold,
    /// rather than drifting back to its owner.
    float stayX = 0, stayY = 0, stayZ = 0;
    std::string name;
    bool operator==(const LocalRealmPet&) const = default;
};

/// GUID space. Kept above the NPC prefix so a summon can never collide with a
/// spawn GUID, and outside the human/bot player range.
inline constexpr uint64_t kLocalPetGuidPrefix = 0xF140ULL << 48;

inline bool localPetGuid(uint64_t guid) { return (guid & (0xFFFFULL << 48)) == kLocalPetGuidPrefix; }

inline bool validLocalPet(const LocalRealmPet& pet) {
    if (!pet.guid || !localPetGuid(pet.guid) || !pet.ownerGuid || pet.guid == pet.ownerGuid) return false;
    if (pet.kind != LocalPetKind::Controlled && pet.kind != LocalPetKind::Guardian) return false;
    if (!pet.entry || !pet.summonSpellId || !pet.level || pet.level > 80) return false;
    if (!pet.maxHealth || pet.health > pet.maxHealth) return false;
    if (pet.resourceType != 255 && pet.resourceType != 0 && pet.resourceType != 2) return false;
    if (pet.resourceType == 255 ? (pet.maxPower || pet.power) : (!pet.maxPower || pet.power > pet.maxPower)) return false;
    if (pet.maxPower > 100000 || pet.powerRegenElapsedMs > kLocalPetFocusRegenIntervalMs) return false;
    if (pet.attackPeriodMs < 200 || pet.attackPeriodMs > 10000) return false;
    if (pet.kind == LocalPetKind::Controlled ? pet.remainingMs != 0 : (!pet.remainingMs || pet.remainingMs > 3600000)) return false;
    if (!std::isfinite(pet.x) || !std::isfinite(pet.y) || !std::isfinite(pet.z) || !std::isfinite(pet.orientation)) return false;
    if (!std::isfinite(pet.attackTimer) || pet.attackTimer < 0 || pet.attackTimer > 60) return false;
    if (pet.name.size() > 96) return false;
    // Only STAY and FOLLOW are command *states*: ATTACK is the separate
    // IsCommandAttack flag and ABANDON is the dismissal itself, so a persisted
    // or replicated pet carrying either is invalid rather than tolerated.
    if (pet.command != LocalPetCommand::Stay && pet.command != LocalPetCommand::Follow) return false;
    if (pet.react != LocalPetReact::Passive && pet.react != LocalPetReact::Defensive &&
        pet.react != LocalPetReact::Aggressive) return false;
    if (!std::isfinite(pet.stayX) || !std::isfinite(pet.stayY) || !std::isfinite(pet.stayZ)) return false;
    // A guardian is not commanded - HandlePetAction needs a CharmInfo, which
    // only a controlled minion has - so it keeps the defaults and no stay point.
    if (pet.kind == LocalPetKind::Guardian &&
        (pet.command != kLocalPetDefaultCommand || pet.commandAttack)) return false;
    // An attack order without a victim is not a state the reference can be in:
    // _stopAttack clears the flag on the same pass that clears the target.
    if (pet.commandAttack && !pet.targetGuid) return false;
    // A pet that is not staying carries no stay point. The converse is NOT
    // asserted: a pet told to stay at the world origin is legal, and a fixture
    // that works at (0,0,0) must not become invalid for saying so.
    if (pet.command != LocalPetCommand::Stay && (pet.stayX || pet.stayY || pet.stayZ)) return false;
    return true;
}

/// PetAI::UpdateAI's target-acquisition gate, reduced to the three stances.
/// REACT_AGGRESSIVE auto-selects; REACT_PASSIVE acquires from none of the four
/// routes, every one of which opens with `if (HasReactState(REACT_PASSIVE))
/// return;` (PetAI.cpp:197-213, 452-510, 832-845); REACT_DEFENSIVE acquires
/// only through AttackedBy / OwnerAttackedBy / OwnerAttacked, never by looking
/// for something to fight.
inline bool localPetAutoAcquires(const LocalRealmPet& pet) {
    return pet.react == LocalPetReact::Aggressive;
}
inline bool localPetDefendsOwner(const LocalRealmPet& pet) {
    return pet.react != LocalPetReact::Passive;
}
/// COMMAND_STAY: the pet does not leave its stay point, and only swings at a
/// victim that walked into melee range unless it was explicitly commanded to
/// attack (PetAI.cpp:186-192).
inline bool localPetHoldsPosition(const LocalRealmPet& pet) {
    return pet.command == LocalPetCommand::Stay;
}
/// PetAI::_stopAttack -> ClearCharmInfoFlags (PetAI.cpp:72-90, 816-831): the
/// attack order goes with the victim, and the command state does NOT, so the
/// pet returns to whichever of stay or follow it was holding.
inline void localPetStopAttack(LocalRealmPet& pet) {
    pet.targetGuid = 0;
    pet.commandAttack = false;
}

/// Pet.cpp::Regenerate. Advances the source interval and returns the focus
/// actually added, so the caller can report a real resource event or nothing.
/// Combat does not suspend focus regeneration in the reference.
inline uint32_t localPetRegenerate(LocalRealmPet& pet, uint32_t elapsedMs) {
    if (pet.dead || pet.resourceType != 2 || !pet.maxPower) return 0;
    pet.powerRegenElapsedMs += std::min(elapsedMs, kLocalPetFocusRegenIntervalMs);
    if (pet.powerRegenElapsedMs < kLocalPetFocusRegenIntervalMs) return 0;
    pet.powerRegenElapsedMs -= kLocalPetFocusRegenIntervalMs;
    const auto before = pet.power;
    pet.power = std::min(pet.maxPower, pet.power + kLocalPetFocusRegenAmount);
    return pet.power - before;
}

/// The recipient half of an owner's energize proc. Returns what was actually
/// granted; a full bar, a dead summon or a mismatched power system grants none.
inline uint32_t localPetRestorePower(LocalRealmPet& pet, uint8_t resourceType, uint32_t amount) {
    if (pet.dead || !pet.maxPower || pet.resourceType != resourceType || !amount) return 0;
    const auto before = pet.power;
    pet.power = uint32_t(std::min(uint64_t(pet.maxPower), uint64_t(before) + amount));
    return pet.power - before;
}

/// Unit::GetCharmerOrOwnerPlayerOrPlayerItself and Unit::GetSpellModOwner both
/// resolve to this player for an owned creature. It is the player that receives
/// experience, quest credit and the loot tag for the summon's killing blow, and
/// the one whose modifiers apply to the summon's own effects. It is NEVER the
/// source or target of the summon's combat events.
inline LocalRealmPlayer* localPetOwner(const LocalRealmPet& pet,
                                       const std::vector<LocalRealmPlayer*>& players) {
    for (auto* p : players)
        if (p && p->guid == pet.ownerGuid && p->mapId == pet.mapId && p->instanceId == pet.instanceId)
            return p;
    return nullptr;
}

/// Whether this summon is still allowed to exist for its owner. Travel, death,
/// logout and a lost owner all retire it immediately rather than at the next
/// catalog refresh.
inline bool localPetOwnerEligible(const LocalRealmPet& pet, const LocalRealmPlayer* owner) {
    return owner && !owner->dead && owner->health && owner->mapId == pet.mapId &&
           owner->instanceId == pet.instanceId && !owner->flight.active;
}

/// Whether a decoded spell is the reviewed controlled-summon profile.
inline bool validLocalSummonPet(const LocalSpellDefinition& d) {
    if (!d.summonPetEntry) return d.summonPetKind == 0 && d.summonPetEffectSlot == 255 && !d.summonPetDurationMs;
    return d.summonPetKind == uint8_t(LocalPetKind::Controlled) && d.summonPetEffectSlot < 3 &&
           !d.summonPetDurationMs && d.clientSpell && !d.passive && !d.triggeredOnly &&
           !d.damage && !d.heal && !d.periodicDamage && !d.periodicHeal && !d.buffHealth &&
           !d.buffArmor && !d.buffAbsorb && !d.manaPer5 && !d.manaPerAbsorbMilli &&
           !d.formId && !d.comboProfile && !d.meleeSpecialProfile && !d.stormstrikeProfile &&
           !d.wardProfile && !d.arcaneBlastProfile && !d.clearcastingProfile &&
           d.proc.effect == LocalProcEffect::None && d.secondaryProc.effect == LocalProcEffect::None;
}

inline bool validLocalPets(const std::vector<LocalRealmPet>& pets) {
    if (pets.size() > kLocalMaxPets) return false;
    for (size_t i = 0; i < pets.size(); ++i) {
        if (!validLocalPet(pets[i])) return false;
        for (size_t j = 0; j < i; ++j) if (pets[i].guid == pets[j].guid) return false;
        size_t controlled = 0, guardians = 0;
        for (const auto& other : pets)
            if (other.ownerGuid == pets[i].ownerGuid)
                (other.kind == LocalPetKind::Controlled ? controlled : guardians)++;
        if (controlled > 1 || guardians > kLocalMaxGuardiansPerOwner) return false;
    }
    return true;
}
}
