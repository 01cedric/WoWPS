#pragma once

#include <cmath>

namespace wowee::core {

// World units. Reject malformed network values before they reach spatial
// queries. This deliberately includes very small forms and large mounts.
inline constexpr float DEFAULT_COLLISION_HEIGHT = 2.0f;
inline bool validCollisionHeight(float height) {
    return std::isfinite(height) && height >= 0.01f && height <= 100.0f;
}
inline float collisionHeightOrDefault(float height) {
    return validCollisionHeight(height) ? height : DEFAULT_COLLISION_HEIGHT;
}

} // namespace wowee::core
