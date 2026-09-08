#pragma once

/// Which light volume comes first when two of them weigh the same.
///
/// Light.dbc's volumes overlap, and a volume whose inner radius contains the
/// player weighs exactly 1.0 - not nearly, exactly, because that branch
/// assigns the constant. On the shipped maps 166 points sit inside two such
/// volumes and 23 inside three, and only the top two are blended.
///
/// So weight alone does not order them. Left to std::sort - which is unstable,
/// and whose treatment of equal elements depends on the length and contents of
/// the whole sequence - the pair chosen changed as the player walked, because
/// distant volumes crossing their outer radius kept changing that sequence.
/// The blended colours are smoothed over time and survived it. The skybox is
/// not: it is whichever volume comes first in this order and names one, and a
/// sky model is swapped outright. That was the sky flickering while moving.
///
/// The tiebreak is the tighter volume, which is the right answer and not
/// merely a fixed one - a small light sits inside a big one because it
/// describes somewhere more specific, so it is the one that should win where
/// both apply fully. Light id decides the last case, so that two volumes of
/// identical extent still cannot swap places between frames.

#include <cstdint>

namespace wowee::rendering {

/// True when `a` should be blended before `b`.
///
/// The arguments are the three fields the order depends on rather than the
/// volume itself, so the rule can be held against a table of cases without a
/// loaded DBC behind it.
inline bool lightVolumeOrderedBefore(float weightA, float outerRadiusA, uint32_t idA,
                                     float weightB, float outerRadiusB, uint32_t idB) {
    if (weightA != weightB) return weightA > weightB;
    if (outerRadiusA != outerRadiusB) return outerRadiusA < outerRadiusB;
    return idA < idB;
}

}  // namespace wowee::rendering
