#pragma once
#include "game/local_pet_spell.hpp"
#include "game/pet_action.hpp"

namespace wowee::game {
/// The existing ten-slot FrameXML pet bar. The first ability slot follows the
/// pet's level, while the owner's persisted toggle follows the whole rank chain.
inline constexpr uint32_t localPetActionBarSlot(unsigned slot, uint32_t entry,
                                               uint8_t level, bool autocast) {
    if (slot == pet::kActionBarSpellStart)
        if (const auto spell = localPetFireboltSpell(entry, level))
            return pet::packPetAction(autocast ? pet::ActionType::Enabled : pet::ActionType::Disabled, spell);
    return pet::defaultPetActionSlot(slot);
}
} // namespace wowee::game
