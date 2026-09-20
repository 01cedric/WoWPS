#pragma once

// How a pet action is packed into the CMSG_PET_ACTION `action` field.
//
// It is not a flat list of ids. The field is a pair: the high byte says what
// kind of thing this is, and the low 24 bits say which one. The kinds reuse the
// same small numbers, so 1 means "follow" as a command and "defensive" as a
// reaction, and the two are only told apart by the byte above them.
//
// Four callers each invented their own reading of this - a raw 1..6 with no
// type byte at all, the type and the action swapped, and a dismiss that packed
// action 0 and so told the pet to stay. The bar slots the server sends down are
// already packed correctly and can be sent straight back; everything the client
// builds itself goes through packPetAction.

#include <cstdint>

namespace wowee {
namespace game {
namespace pet {

/// High byte: what kind of action this is.
enum class ActionType : uint8_t {
    Decide   = 0x00,
    Passive  = 0x01,
    Reaction = 0x06,  ///< Passive / defensive / aggressive stance.
    Command  = 0x07,  ///< Stay / follow / attack / dismiss.
    Disabled = 0x81,  ///< Castable spell, autocast off.
    Enabled  = 0xC1,  ///< Castable spell, autocast on.
};

/// Low 24 bits when the type is Command. These are CommandStates
/// (AzerothCore 9c416aaa, Unit.h:574-577) and there are exactly four: the
/// `ACT_COMMAND` switch in WorldSession::HandlePetActionHelper
/// (PetHandler.cpp:162-304) has these four cases and a `default:` that logs
/// "unknown PET flag Action". An earlier revision of this header carried a
/// fifth, `kMoveTo = 4`; there is no `COMMAND_MOVE_TO` at build 12340 and no
/// caller ever sent it, so it is removed rather than implemented. See
/// the source audit section 7, correction 5.
enum Command : uint32_t {
    kStay    = 0,
    kFollow  = 1,
    kAttack  = 2,
    kAbandon = 3,  ///< Dismiss. Not present in the default bar - built by hand.
};
inline constexpr uint32_t kCommandCount = 4;

/// Low 24 bits when the type is Reaction. ReactStates, Unit.h:567-569.
enum Reaction : uint32_t {
    kPassive    = 0,
    kDefensive  = 1,
    kAggressive = 2,
};
inline constexpr uint32_t kReactionCount = 3;

constexpr uint32_t packPetAction(ActionType type, uint32_t action) {
    return (static_cast<uint32_t>(type) << 24) | (action & 0x00FFFFFFu);
}

constexpr uint32_t petActionId(uint32_t packed) { return packed & 0x00FFFFFFu; }
constexpr ActionType petActionType(uint32_t packed) {
    return static_cast<ActionType>((packed >> 24) & 0xFFu);
}

/// Whether a packed word is one the authority will act on: a command 0-3, a
/// reaction 0-2, or a spell slot. Anything else is HandlePetActionHelper's
/// `default:` and is refused rather than reinterpreted.
constexpr bool validPetActionId(ActionType type, uint32_t action) {
    if (type == ActionType::Command) return action < kCommandCount;
    if (type == ActionType::Reaction) return action < kReactionCount;
    return type == ActionType::Disabled || type == ActionType::Enabled ||
           type == ActionType::Passive || type == ActionType::Decide;
}

/// The ten default bar slots CharmInfo::InitPetActionBar builds
/// (CharmInfo.cpp, ActionBarIndex in CharmInfo.h:51-57): three commands as
/// `COMMAND_ATTACK - i`, four empty spell slots, three reactions the same way.
inline constexpr unsigned kActionBarSlots = 10;
inline constexpr unsigned kActionBarSpellStart = 3;
inline constexpr unsigned kActionBarSpellEnd = 7;
constexpr uint32_t defaultPetActionSlot(unsigned slot) {
    if (slot < kActionBarSpellStart)
        return packPetAction(ActionType::Command, kAttack - slot);
    if (slot < kActionBarSpellEnd) return packPetAction(ActionType::Passive, 0);
    if (slot < kActionBarSlots)
        return packPetAction(ActionType::Reaction, kAggressive - (slot - kActionBarSpellEnd));
    return 0;
}

/// Whether a packed slot holds a castable spell rather than a built-in command
/// or stance. The spell types are the ones that carry an autocast bit.
constexpr bool isPetSpellAction(uint32_t packed) {
    const ActionType t = petActionType(packed);
    return t != ActionType::Command && t != ActionType::Reaction;
}

/// Zero is a real command/reaction ID (Stay/Passive), but an empty spell slot.
constexpr bool populatedPetActionSlot(uint32_t packed) {
    return packed && validPetActionId(petActionType(packed), petActionId(packed)) &&
        (!isPetSpellAction(packed) || petActionId(packed) != 0);
}

} // namespace pet
} // namespace game
} // namespace wowee
