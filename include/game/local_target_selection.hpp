#pragma once
#include <cmath>
#include <cstdint>
namespace wowee::game {
// Console targeting is repeatable nearest selection, including quest NPCs.
// Compare canonical 3D positions; GUID resolves equal-distance ties.
template<class Player, class Npcs>
uint64_t nearestLivingLocalTarget(const Player& player, const Npcs& npcs, float range = 40.0f) {
    uint64_t best = 0;
    float bestDistance = range * range;
    for (const auto& npc : npcs) {
        if (!npc.guid || npc.dead || npc.mapId != player.mapId || npc.instanceId != player.instanceId) continue;
        const float x = npc.x-player.x, y = npc.y-player.y, z = npc.z-player.z;
        const float d = x*x+y*y+z*z;
        if (!std::isfinite(d) || d > bestDistance) continue;
        if (!best || d < bestDistance || npc.guid < best) { best = npc.guid; bestDistance = d; }
    }
    return best;
}
} // namespace wowee::game
