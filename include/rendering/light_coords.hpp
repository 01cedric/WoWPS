#pragma once

/// Where Light.dbc says a light volume is, in world space.
///
/// The file stores positions and falloff radii in thirty-sixths of a yard, on
/// the tile grid's own axes: the origin is a corner of the 64x64 grid rather
/// than its centre, and the two horizontal axes are mirrored and swapped
/// against world space.
///
/// None of that was applied. The scale was 1.0, with a comment saying to try
/// 36 if distances seemed off, and they were: a player standing in Tirisfal is
/// 2372 units from the nearest of map 0's 82 volumes, whose outer radius is 0.
/// No volume matched anywhere, on any map, so every zone fell back to default
/// lighting and looked like an ordinary bright day, and no zone ever resolved
/// a skybox - the active skybox path is only filled by walking the volumes
/// that matched.
///
/// 36 is not a guess. Divided by it the radii land on the tile grid: light 2's
/// inner radius of 19200 is 533.33 yards, exactly one ADT tile.
///
/// The axis mapping is checked against zones whose world coordinates are
/// known. It is the same mirrored-and-swapped pairing the terrain uses, and
/// the one core/coordinates.hpp warns not to "fix".

#include <glm/glm.hpp>

#include "core/coordinates.hpp"

namespace wowee::rendering {

/// Thirty-sixths of a yard: Light.dbc's unit for positions and radii.
inline constexpr float LIGHT_COORD_UNITS_PER_YARD = 36.0f;

/// One Light.dbc position, in world space.
///
/// `dbcX`, `dbcY` and `dbcZ` are the file's own fields, unconverted.
inline glm::vec3 lightPositionToWorld(float dbcX, float dbcY, float dbcZ) {
    const float toYards = 1.0f / LIGHT_COORD_UNITS_PER_YARD;
    return glm::vec3(core::coords::ZEROPOINT - dbcY * toYards,
                     core::coords::ZEROPOINT - dbcX * toYards,
                     dbcZ * toYards);
}

}  // namespace wowee::rendering
