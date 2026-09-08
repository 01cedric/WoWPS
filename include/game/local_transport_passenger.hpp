#pragma once
#include "game/local_travel.hpp"
#include <cmath>

namespace wowee::game {
struct LocalPassengerPose { float x, y, z, orientation; };
// The route heading points forward; the authored hull's bow points along -X.
// Crew coordinates come from creature rows on the transport's own Map.dbc map.
inline LocalPassengerPose localTransportPassengerPose(const LocalTransportState& hull,
    float x, float y, float z, float orientation) {
    constexpr float pi = 3.14159265358979323846f;
    const float yaw = hull.orientation + pi;
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {hull.x+c*x-s*y, hull.y+s*x+c*y, hull.z+z,
            std::remainder(yaw+orientation,2.0f*pi)};
}
} // namespace wowee::game
