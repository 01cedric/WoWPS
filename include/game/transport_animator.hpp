// include/game/transport_animator.hpp
// Path evaluation, Z clamping, and orientation for transports.
// Extracted from TransportManager (Phase 3b of spline refactoring).
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>

namespace wowee::math { class CatmullRomSpline; }

namespace wowee::game {

struct ActiveTransport;
struct PathEntry;

/// Evaluates a transport's position and orientation from a spline path and path time.
class TransportAnimator {
public:
    /// Evaluate the spline at pathTimeMs, apply Z clamping, and update
    /// transport.position and transport.rotation in-place.
    void evaluateAndApply(
        ActiveTransport& transport,
        const PathEntry& pathEntry,
        uint32_t pathTimeMs) const;

private:
    /// Guard against bad fallback Z offsets on non-world-coordinate paths.
    static float clampZOffset(float z, bool worldCoords, bool clientAnim,
                              int serverUpdateCount, bool hasServerClock);
};

} // namespace wowee::game
