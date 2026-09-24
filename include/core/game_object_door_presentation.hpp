#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace wowee::core {

enum class GameObjectDoorPose : uint8_t {
    Closed = 0,
    Open = 1,
};

struct GameObjectDoorPoseState {
    GameObjectDoorPose pose = GameObjectDoorPose::Closed;
    uint32_t revision = 0;
};

// Renderer-independent authority cache for streamed door presentation. A state
// can arrive before the model; the spawner reads it once asynchronous placement
// finishes. Entries survive a distance despawn and retire on a context change.
class GameObjectDoorPresentationCache {
public:
    bool setContext(uint64_t context) {
        if (hasContext_ && context_ == context) return false;
        context_ = context;
        hasContext_ = true;
        states_.clear();
        return true;
    }

    // Repeating the exact revision and pose is an accepted no-op. A conflicting
    // value at the same revision is rejected so packet order cannot decide it.
    bool publish(uint64_t guid, GameObjectDoorPose pose, uint32_t revision) {
        if (!guid || !revision) return false;
        const auto it = states_.find(guid);
        if (it != states_.end()) {
            if (revision == it->second.revision) return pose == it->second.pose;
            // Shared object revisions are non-zero uint32 serials and wrap
            // UINT32_MAX -> 1. RFC-1982-style half-range ordering keeps that
            // wrap newer while rejecting a delayed pre-wrap packet.
            const uint32_t delta = revision - it->second.revision;
            if (!delta || delta >= 0x80000000u) return false;
        }
        states_[guid] = {pose, revision};
        return true;
    }

    [[nodiscard]] std::optional<GameObjectDoorPoseState> lookup(uint64_t guid) const {
        const auto it = states_.find(guid);
        if (it == states_.end()) return std::nullopt;
        return it->second;
    }

    void clear() {
        states_.clear();
        hasContext_ = false;
        context_ = 0;
    }

    [[nodiscard]] size_t size() const { return states_.size(); }

private:
    uint64_t context_ = 0;
    bool hasContext_ = false;
    std::unordered_map<uint64_t, GameObjectDoorPoseState> states_;
};

} // namespace wowee::core
