#pragma once

/**
 * shadow_quality.hpp - what the extShadowQuality levels mean, and which one the
 * client ships at.
 *
 * The setting carries two things at once and it is worth being explicit about
 * which is which, because only one of them can change while the client runs.
 *
 *   scope       what casts into the map. 0 nothing, 1 the terrain only, 2 and
 *               above everything that stands on it - WMOs, doodads, creatures,
 *               NPCs and every player character. Read every frame by
 *               Renderer::renderShadowPass, so the dropdown takes effect at
 *               once.
 *   resolution  how big the map is. Chosen once, because the depth images are
 *               created at start-up and never rebuilt; the panel marks the
 *               control as needing a restart for this half alone.
 *
 * The default lives here rather than at either reader, because three of them
 * have to agree: the renderer, which reads the CVar file before any interface
 * exists; GetCVar, answering a panel the player has never opened; and the
 * replay of stored values at login. A disagreement between them is a setting
 * that reads as one thing in the options and behaves as another in the world.
 */

#include <cstdint>

namespace wowee {
namespace rendering {

/// The shipped default, as the string the CVar store holds.
///
/// Two, on every platform, because that is the first level at which anything
/// but the ground casts a shadow - and a world where buildings, trees and other
/// players cast nothing is the complaint this setting exists to answer. What it
/// costs the console is bounded: see kShadowSideForLevel in Renderer::initialize
/// for why level 2 there is the same 1024 map level 1 is.
inline constexpr const char* kShadowQualityDefaultCVar = "2";

/// The highest level the dropdown offers. Above 2 the scope is unchanged and
/// only the map grows, which is why the top two levels are the same size on
/// hardware that cannot spend the memory.
inline constexpr int kShadowQualityMaxLevel = 4;

/// True when this level draws anything at all into the map. Level 0 still runs
/// the pass - it is what clears the image and leaves it in the layout its
/// readers expect - and simply submits no casters.
[[nodiscard]] inline constexpr bool shadowLevelDrawsCasters(int level) {
    return level > 0;
}

/// True when objects standing on the ground cast, not only the ground itself.
[[nodiscard]] inline constexpr bool shadowLevelDrawsObjects(int level) {
    return level >= 2;
}

}  // namespace rendering
}  // namespace wowee
