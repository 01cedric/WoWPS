#pragma once

#include "game/local_gameplay.hpp"

namespace wowee::game {

// A crew shares its hull's established threat, but an enemy that no current
// occupant can interact with must not damage the hull through a phase change.
// An empty hull keeps already-established threat until the ordinary leash
// removes it. This helper does not acquire new enemies for an empty vehicle.
// The callback is the gameplay authority's canAttack(player, hostile) predicate.
template <class CanAttack>
inline bool localVehicleHostileTargetValid(
    const LocalRealmNpc& hull, const LocalRealmNpc& hostile,
    const std::vector<LocalRealmPlayer*>& players, CanAttack canAttack) {
    if (!hull.vehicleId || hull.dead || !hull.health || hostile.dead || !hostile.health ||
        hull.mapId != hostile.mapId || hull.instanceId != hostile.instanceId)
        return false;

    bool hasCrew = false;
    for (const auto* rider : players) {
        if (!rider || rider->vehicleGuid != hull.guid || rider->dead || rider->ghost ||
            !rider->health || rider->flight.active || rider->transportEntry ||
            rider->mapId != hull.mapId || rider->instanceId != hull.instanceId)
            continue;
        hasCrew = true;
        if (rider->vehicleId == hull.vehicleId && rider->vehicleSeat < hull.vehicleSeatCount &&
            localPhaseVisible(rider->phaseMask, hull.requiredPhaseMask, hull.excludedPhaseMask) &&
            canAttack(*rider, hostile))
            return true;
    }
    return !hasCrew;
}

} // namespace wowee::game
