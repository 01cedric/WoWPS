#pragma once

#include "game/local_gameplay.hpp"
#include "game/local_travel.hpp"
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace wowee::core {
// CPU-only identity and deck-local pose. No pointer, renderer instance ID or GPU
// allocation is transferable between maps. The realm retains gameplay state.
struct TransportPassengerPose {
    uint64_t guid = 0;
    std::array<float, 3> offset{};
    float orientation = 0;
    bool crew = false;
};
struct TransportTransferSnapshot {
    game::LocalTransportState hull{};
    uint32_t displayId = 0;
    uint32_t pathId = 0;
    TransportPassengerPose localPlayer;
    std::vector<TransportPassengerPose> passengers;
};

inline std::optional<TransportTransferSnapshot> captureTransportTransfer(
    const game::LocalRealmPlayer& self, uint32_t destinationMap,
    const std::vector<game::LocalTransportState>& hulls,
    const std::vector<game::LocalTransportRoute>& routes,
    const std::vector<game::LocalRealmPlayer>& players,
    const std::vector<game::LocalRealmNpc>& npcs) {
    if (!self.transportEntry || self.instanceId || self.mapId != destinationMap) return {};
    const game::LocalTransportState* hull = nullptr;
    for (const auto& h : hulls)
        if (h.entry == self.transportEntry && h.mapId == destinationMap) { hull = &h; break; }
    if (!hull) return {};
    const game::LocalTransportRoute* route = nullptr;
    for (const auto& r : routes)
        if (r.entry == hull->entry && r.displayId) { route = &r; break; }
    if (!route) return {};
    TransportTransferSnapshot snapshot;
    snapshot.hull = *hull;
    snapshot.displayId = route->displayId;
    snapshot.pathId = route->pathId;
    snapshot.localPlayer = {self.guid, {self.transportOffsetX, self.transportOffsetY,
                                      self.transportOffsetZ}, std::remainder(self.orientation - hull->orientation, 6.28318530718f), false};
    snapshot.passengers.push_back(snapshot.localPlayer);
    for (const auto& player : players)
        if (player.guid != self.guid && player.transportEntry == hull->entry &&
            player.mapId == destinationMap && !player.instanceId)
            snapshot.passengers.push_back({player.guid, {player.transportOffsetX,
                player.transportOffsetY, player.transportOffsetZ},
                std::remainder(player.orientation - hull->orientation, 6.28318530718f), false});
    for (const auto& npc : npcs)
        if (npc.transportEntry == hull->entry && npc.mapId == destinationMap && !npc.instanceId)
            snapshot.passengers.push_back({npc.guid, {npc.transportX, npc.transportY,
                npc.transportZ}, npc.transportOrientation, true});
    return snapshot;
}
} // namespace wowee::core
