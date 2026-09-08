#pragma once

#include <algorithm>

namespace wowee {
namespace rendering {

// A resident cache may contain extra tiles behind the camera. Cache size must
// not change the draw radius or slowly fade the scene away after world entry.
inline float m2StableRenderDistance(float viewDistance, float environmentDetail) {
    return std::max(0.0f, viewDistance) * std::clamp(environmentDetail, 0.0f, 1.0f);
}

/// The distance at which one M2 instance stops drawing, squared.
///
/// Three separate inputs each push an instance further out than the scene's
/// base distance: a large model gets a factor above 1 so a cathedral does not
/// pop before a shrub does, an animation-disabled model gets a further 2.6x,
/// and a server game object gets an outright floor so a mailbox stays findable.
/// All three are biases within the drawn world, not licence to leave it. The
/// terrain and the WMOs stop at the view distance the player asked for, so an
/// instance past that ceiling is a tree standing on nothing - which is why the
/// clamp comes last, after every widening rather than before it.
inline float m2InstanceMaxDistSq(float baseMaxDistSq,
                                 float perInstanceFactor,
                                 bool isGameObject,
                                 float gameObjectMinDistance,
                                 float viewDistanceAbsolute,
                                 bool isGroundDetail = false,
                                 float groundDetailMaxDistance = 0.0f) {
    float maxDistSq = baseMaxDistSq * perInstanceFactor;
    if (isGameObject) {
        maxDistSq = std::max(maxDistSq, gameObjectMinDistance * gameObjectMinDistance);
    }
    // Grass and the rest of the ground cover stop far closer than anything
    // else, and are their own setting for it: the original client draws them
    // between 70 and 140 yards while doodads go out to the horizon. A ceiling
    // rather than a scale, because it is a distance the player names outright.
    if (isGroundDetail && groundDetailMaxDistance > 0.0f) {
        maxDistSq = std::min(maxDistSq, groundDetailMaxDistance * groundDetailMaxDistance);
    }
    return std::min(maxDistSq, viewDistanceAbsolute * viewDistanceAbsolute);
}

} // namespace rendering
} // namespace wowee
