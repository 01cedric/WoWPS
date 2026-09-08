#pragma once
#include "rendering/ps4_world_budget.hpp"

/// The graphics defaults a fresh install starts at.
///
/// Spelled in three places before: the settings panel's pending fields, the
/// login screen's copy of the same struct, and the reset button's constant. A
/// default that disagrees with itself is a setting that changes when you open
/// a different window.
///
/// They live here rather than in settings_panel.hpp because that header
/// includes vulkan.h, and a test that only wants to know the numbers should
/// not need the Vulkan SDK to find out - which it does not have on macOS.

namespace wowee::ui {

#ifdef WOWEE_PS4
inline constexpr float kDefaultViewDistance = rendering::ps4budget::kDefaultViewDistance;
#else
inline constexpr float kDefaultViewDistance = 1900.0f;
#endif
inline constexpr int   kDefaultGroundClutter = 70;

}  // namespace wowee::ui
