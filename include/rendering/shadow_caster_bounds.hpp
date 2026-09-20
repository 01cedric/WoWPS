#pragma once
#include <glm/glm.hpp>
#include <cmath>

namespace wowee::rendering {
// Conservative world AABB versus the finite orthographic shadow volume.
// Unlike a camera-centered sphere this retains upstream low-sun casters.
// The matrix is the actual Vulkan projection (NDC depth 0..1), w=1.
inline bool shadowIntersectsWorldBounds(const glm::mat4& lightSpace,
                                       const glm::vec3& low, const glm::vec3& high) {
    const glm::vec3 center = (low + high) * 0.5f;
    const glm::vec3 extent = glm::abs(high - low) * 0.5f;
    const glm::vec4 projected = lightSpace * glm::vec4(center, 1.0f);
    glm::vec3 radius;
    for (int axis = 0; axis != 3; ++axis) {
        radius[axis] = std::abs(lightSpace[0][axis]) * extent.x +
                       std::abs(lightSpace[1][axis]) * extent.y +
                       std::abs(lightSpace[2][axis]) * extent.z;
    }
    // Negated outside tests keep malformed/non-finite bounds conservative.
    return !(projected.x + radius.x < -1.0f || projected.x - radius.x > 1.0f ||
             projected.y + radius.y < -1.0f || projected.y - radius.y > 1.0f ||
             projected.z + radius.z <  0.0f || projected.z - radius.z > 1.0f);
}
}
