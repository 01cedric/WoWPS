#pragma once
#include <cmath>
#include <cstdint>
namespace wowee::rendering {
// Conservative bound: Frobenius norm >= spectral norm, including nonuniform
// scale and shear. Caller computes normSquared from all nine linear entries.
inline bool m2FarShadowSubtexel(uint32_t pass, float threshold,
                               float radius, float normSquared, bool wind) {
    if (pass != 1 || !(threshold > 0.0f) || !std::isfinite(threshold) ||
        !(radius >= 0.0f) || !std::isfinite(radius) ||
        !(normSquared >= 0.0f) || !std::isfinite(normSquared)) return false;
    // Wind maximum local displacements: x=.35+.15+.06, y=.25+.12+.05.
    // hypot(.56,.42)=.70. Round upward for float arithmetic.
    const float paddedRadius = radius + (wind ? 0.701f : 0.001f);
    // Compare squared diameters. The far pass evaluates this for thousands of
    // doodads every frame on Jaguar; sqrt here was pure scalar cost. Inputs are
    // already finite/non-negative, and using double for the products keeps the
    // threshold decision conservative around float rounding boundaries.
    const double diameterSquared = 4.0 * static_cast<double>(paddedRadius) *
        static_cast<double>(paddedRadius) * static_cast<double>(normSquared);
    const double thresholdSquared = static_cast<double>(threshold) * threshold;
    return diameterSquared < thresholdSquared;
}
}
