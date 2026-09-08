#pragma once
#include <algorithm>
#include <cmath>

namespace wowee::game {
// The model origin is not the deck: zeppelin decks are BELOW it, ships above.
// These are only broad-phase limits. Boarding always requires a floor belonging
// to this exact hull; proximity to its dock is not evidence of being aboard.
struct TransportDeckContact {
    float unsupportedSeconds = 0.0f;
    static bool within(float x, float y, float z, float margin = 0.0f) {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
            std::abs(x) <= 65.0f + margin && std::abs(y) <= 30.0f + margin &&
            std::abs(z) <= 60.0f + margin;
    }
    static bool canBoard(float x, float y, float z, bool hullFloor) {
        return within(x, y, z) && hullFloor;
    }
    void reset() { unsupportedSeconds = 0.0f; }
    bool shouldLeave(float x, float y, float z, bool hullFloor,
                     bool collisionReady, float seconds) {
        if (!within(x, y, z, 5.0f)) { reset(); return true; }
        // Destination hull collision can arrive after its visual during a map
        // transfer. Do not mistake this for a measured absence of deck.
        if (!collisionReady || hullFloor) { reset(); return false; }
        if (std::isfinite(seconds) && seconds > 0.0f)
            unsupportedSeconds += std::min(seconds, 1.0f);
        // Wall-time based, not twenty frames (ten seconds at two FPS).
        return unsupportedSeconds >= 0.75f;
    }
};
} // namespace wowee::game
