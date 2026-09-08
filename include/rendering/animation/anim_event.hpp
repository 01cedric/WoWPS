#pragma once

#include <cstdint>

namespace wowee {
namespace rendering {

// ============================================================================
// AnimEvent
//
// Event-driven animation state transitions. Sub-FSMs react to these events
// instead of polling conditions every frame.
// ============================================================================
enum class AnimEvent : uint8_t {
    MOVE_START, MOVE_STOP,
    SPRINT_START, SPRINT_STOP,
    JUMP, LANDED,
    SWIM_ENTER, SWIM_EXIT,
    COMBAT_ENTER, COMBAT_EXIT,
    STUN_ENTER, STUN_EXIT,
    SPELL_START, SPELL_STOP,
    HIT_REACT, CHARGE_START, CHARGE_END,
    EMOTE_START, EMOTE_STOP,
    LOOT_START, LOOT_STOP,
    SIT, STAND_UP,
    MOUNT, DISMOUNT,
    STEALTH_ENTER, STEALTH_EXIT,
};

} // namespace rendering
} // namespace wowee
