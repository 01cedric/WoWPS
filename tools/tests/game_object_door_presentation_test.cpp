#include "core/game_object_door_presentation.hpp"
#include "rendering/m2_animation_clock.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace wowee::core;

int main() {
    GameObjectDoorPresentationCache cache;
    constexpr uint64_t guid = 0xf110000000000321ULL;

    assert(cache.setContext(11));
    assert(cache.publish(guid, GameObjectDoorPose::Open, 4));

    // The authority update precedes the (simulated) streamed spawn. Lookup at
    // spawn time must still recover the exact state and revision.
    const auto atSpawn = cache.lookup(guid);
    assert(atSpawn);
    assert(atSpawn->pose == GameObjectDoorPose::Open);
    assert(atSpawn->revision == 4);

    assert(!cache.publish(guid, GameObjectDoorPose::Closed, 3));
    assert(cache.lookup(guid)->pose == GameObjectDoorPose::Open);
    assert(cache.lookup(guid)->revision == 4);

    // Identical delivery is idempotent; conflicting data with the same
    // revision is rejected instead of becoming packet-order dependent.
    assert(cache.publish(guid, GameObjectDoorPose::Open, 4));
    assert(cache.size() == 1);
    assert(!cache.publish(guid, GameObjectDoorPose::Closed, 4));
    assert(cache.lookup(guid)->pose == GameObjectDoorPose::Open);

    assert(cache.publish(guid, GameObjectDoorPose::Closed, 5));
    assert(cache.publish(guid, GameObjectDoorPose::Closed, 5));
    assert(cache.lookup(guid)->pose == GameObjectDoorPose::Closed);
    assert(cache.lookup(guid)->revision == 5);

    // Authority revisions skip zero and wrap UINT32_MAX -> 1. A delayed frame
    // from before the wrap must remain stale after the new state is accepted.
    constexpr uint64_t wrappedGuid = guid + 1;
    assert(cache.publish(wrappedGuid, GameObjectDoorPose::Closed, UINT32_MAX));
    assert(cache.publish(wrappedGuid, GameObjectDoorPose::Open, 1));
    assert(cache.lookup(wrappedGuid)->pose == GameObjectDoorPose::Open);
    assert(!cache.publish(wrappedGuid, GameObjectDoorPose::Closed, UINT32_MAX));
    assert(!cache.publish(wrappedGuid, GameObjectDoorPose::Closed, 0x80000001u));

    // Reasserting the same context preserves stream state; entering a new one
    // clears it so deterministic GUID reuse cannot leak a pose across realms.
    assert(!cache.setContext(11));
    assert(cache.lookup(guid));
    assert(cache.setContext(12));
    assert(!cache.lookup(guid));
    assert(cache.size() == 0);

    // End-pose M2s use the same zero-speed policy as the renderer.  The two
    // update passes must preserve the exact representable final sample even
    // across large frames, and the variation/loop gates remain disabled.
    float endPose = std::nextafter(1000.0f, 0.0f);
    const float exactEndPose = endPose;
    for (const float frameMs : {0.01f, 16.6667f, 1000.0f, 1000000.0f}) {
        wowee::rendering::m2AdvanceAnimationBase(endPose, frameMs, 0.0f);
        wowee::rendering::m2ApplyAnimationSpeed(endPose, frameMs, 0.0f);
        assert(!wowee::rendering::m2AnimationClockRuns(0.0f));
        assert(endPose == exactEndPose);
    }

    std::cout << "PASS buffered spawn, stale rejection, context reset, pose idempotency, and stable frozen M2 end pose\n";
}
