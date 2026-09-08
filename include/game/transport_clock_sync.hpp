// include/game/transport_clock_sync.hpp
// Clock synchronization and yaw correction for transports.
// Extracted from TransportManager (Phase 3a of spline refactoring).
#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace wowee::math { class CatmullRomSpline; }

namespace wowee::game {

struct ActiveTransport;
struct PathEntry;

/// Manages clock sync, server-yaw correction, and velocity bootstrap for a single transport.
class TransportClockSync {
public:
    /// Compute pathTimeMs for a transport given current elapsed time and deltaTime.
    /// Returns false if the transport should use raw server position (no interpolation).
    [[nodiscard]] bool computePathTime(
        ActiveTransport& transport,
        const math::CatmullRomSpline& spline,
        double elapsedTime,
        float deltaTime,
        uint32_t& outPathTimeMs) const;

    /// Process a server position update: update clock offset, detect yaw flips,
    /// bootstrap velocity, and switch between client/server driven modes.
    void processServerUpdate(
        ActiveTransport& transport,
        const PathEntry* pathEntry,
        const glm::vec3& position,
        float orientation,
        double elapsedTime);

private:
    /// Detect and apply 180-degree yaw correction based on movement vs heading alignment.
    void updateYawAlignment(
        ActiveTransport& transport,
        const glm::vec3& velocity) const;

    /// Bootstrap velocity from nearest DBC path segment on first authoritative sample.
    void bootstrapVelocityFromPath(
        ActiveTransport& transport,
        const PathEntry& pathEntry) const;
};

} // namespace wowee::game
