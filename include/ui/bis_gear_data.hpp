// ============================================================
// Curated "max out" gear sets per expansion + class.
//
// These are NOT strictly-optimal, patch-exact BiS lists - assembling those
// for every class across every raid tier would be enormous and quickly
// outdated. Instead each class gets a compact, iconic endgame kit (anchored
// on its legendary weapon for the expansion where one exists) plus a couple
// of universal staples. It is intended as a starting point that is trivial to
// extend: just add item IDs to the lists in bis_gear_data.cpp. The server
// validates every .additem, so any ID that doesn't exist on that core is
// simply skipped.
// ============================================================
#pragma once

#include <cstdint>
#include <vector>

namespace wowee {
namespace ui {

// Returns the curated item-ID list for the given expansion ("classic", "tbc",
// "wotlk") and class id (1=Warrior ... 11=Druid). Empty if none is defined.
std::vector<uint32_t> getMaxOutGear(const char* expansion, uint8_t classId);

} // namespace ui
} // namespace wowee
