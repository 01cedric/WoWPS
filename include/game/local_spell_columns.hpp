#pragma once
#include <cstdint>

namespace wowee::game::spell335 {
// Spell.dbc build 12340, zero-based 32-bit columns. These are distinct fields:
// casting interruption flags are not proc triggers, and proc chance is not stacking.
// Reference: AzerothCore DBCStructure.h, SpellEntry columns 31-49.
inline constexpr uint32_t InterruptFlags=31;
inline constexpr uint32_t AuraInterruptFlags=32;
inline constexpr uint32_t ChannelInterruptFlags=33;
inline constexpr uint32_t ProcFlags=34;
inline constexpr uint32_t ProcChance=35;
inline constexpr uint32_t ProcCharges=36;
inline constexpr uint32_t StackAmount=49;
inline constexpr uint32_t EffectClassMask=122; // Three 96-bit masks, one per effect.
inline constexpr uint32_t SpellFamily=208;
inline constexpr uint32_t SpellFamilyFlags=209; // Three 32-bit words.
inline constexpr uint32_t EffectValueMultiplier=101; // Three effect columns, 101-103.
}
