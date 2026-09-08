#pragma once

#include <cmath>

namespace wowee::rendering::movement {

// The retail client rejects ground steeper than 50 degrees and steps higher
// than roughly 0.6 yards. Keep these limits shared by terrain, WMO, and M2
// collision paths so release builds cannot silently diverge by surface type.
inline constexpr float kMaxWalkableSlopeDegrees = 50.0f;
inline constexpr float kMinWalkableNormalZ = 0.642787635f; // cos(50 degrees)
inline constexpr float kMaxStepUp = 0.60f;

inline bool isWalkableNormal(float normalZ) {
    return normalZ >= kMinWalkableNormalZ;
}

/// The upward component of the heightfield's normal at a point, from four
/// samples around it.
///
/// The terrain query answers a height and nothing else, so the slope limit that
/// governs WMO and M2 floors was never applied to the ground itself - the
/// constant for it existed and was only ever used as the fallback limit for
/// those other two. A mountain of any steepness was therefore walkable, held
/// back by nothing but the per-step height budget, which a smooth heightfield
/// never trips. This recovers the missing normal by finite difference.
///
/// sample() returns the height at a point, or nothing where there is no ground
/// - at a hole, or off the loaded tiles. A missing neighbour makes the slope
/// unknowable rather than steep, so the answer is 1 and the caller lets it
/// pass: refusing on absent data would stop the player at every tile edge.
template <typename SampleFn>
float heightfieldNormalZ(SampleFn&& sample, float x, float y, float spacing) {
    const auto west  = sample(x - spacing, y);
    const auto east  = sample(x + spacing, y);
    const auto south = sample(x, y - spacing);
    const auto north = sample(x, y + spacing);
    if (!west || !east || !south || !north) return 1.0f;

    const float dzdx = (*east - *west) / (2.0f * spacing);
    const float dzdy = (*north - *south) / (2.0f * spacing);
    return 1.0f / std::sqrt(dzdx * dzdx + dzdy * dzdy + 1.0f);
}

inline bool isReachableStep(float deltaZ) {
    return deltaZ >= -0.25f && deltaZ <= kMaxStepUp;
}

/// Whether the outdoor heightfield at this spot is a roof rather than a floor.
///
/// Inside an interior WMO group - Undercity's halls, a building's rooms - the
/// heightfield overhead is meaningless, and letting it stand as a floor
/// candidate kicks the player up to the surface whenever the WMO floor query
/// finds nothing underfoot.
///
/// But being "inside" is decided by bounding-box containment, and an
/// underground WMO's interior box reaches up through the ground above it. So
/// standing on the hillside over a cave counts as inside, and discarding the
/// terrain there leaves the WMO as the only candidate - whose nearest surface
/// below is the cave's ceiling. The player drops through the hill they are
/// walking on and stands on the roof of the room underneath.
///
/// Hence the height test as well as the containment one: ground higher than the
/// player could step onto cannot be the ground they are standing on, which is
/// the whole of what the veto ever meant. Undercity's surface sits ~113m above
/// its halls and is still refused; a hillside at the feet is kept.
inline bool terrainIsOverheadRoof(bool insideInteriorWmo, float terrainZ,
                                  float feetZ, float stepUpBudget) {
    return insideInteriorWmo && terrainZ > feetZ + stepUpBudget + 0.5f;
}

} // namespace wowee::rendering::movement
