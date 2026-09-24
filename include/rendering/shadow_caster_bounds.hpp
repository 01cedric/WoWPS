#pragma once
#include <glm/glm.hpp>
#include <cmath>

namespace wowee::rendering {

// Prepared form of the conservative AABB-versus-shadow-volume test.  WMO
// shadow submission can execute this thousands of times with the same light
// matrix (and again with one instance-local light matrix).  The old helper
// recomputed the absolute value of all nine linear coefficients for every
// range.  Keep those coefficients once and leave the exact conservative test
// unchanged.
class ShadowBoundsTester {
public:
    explicit ShadowBoundsTester(const glm::mat4& lightSpace) noexcept
        : lightSpace_(lightSpace),
          absX_(std::abs(lightSpace[0][0]), std::abs(lightSpace[1][0]), std::abs(lightSpace[2][0])),
          absY_(std::abs(lightSpace[0][1]), std::abs(lightSpace[1][1]), std::abs(lightSpace[2][1])),
          absZ_(std::abs(lightSpace[0][2]), std::abs(lightSpace[1][2]), std::abs(lightSpace[2][2])) {}

    [[nodiscard]] bool intersects(const glm::vec3& low, const glm::vec3& high) const noexcept {
        const glm::vec3 center = (low + high) * 0.5f;
        const glm::vec3 extent = glm::abs(high - low) * 0.5f;
        const glm::vec4 projected = lightSpace_ * glm::vec4(center, 1.0f);
        const glm::vec3 radius(glm::dot(absX_, extent),
                               glm::dot(absY_, extent),
                               glm::dot(absZ_, extent));
        // Negated outside tests keep malformed/non-finite bounds conservative.
        return !(projected.x + radius.x < -1.0f || projected.x - radius.x > 1.0f ||
                 projected.y + radius.y < -1.0f || projected.y - radius.y > 1.0f ||
                 projected.z + radius.z <  0.0f || projected.z - radius.z > 1.0f);
    }

private:
    glm::mat4 lightSpace_;
    glm::vec3 absX_;
    glm::vec3 absY_;
    glm::vec3 absZ_;
};

// Conservative world AABB versus the finite orthographic shadow volume.
// Unlike a camera-centered sphere this retains upstream low-sun casters.
// The matrix is the actual Vulkan projection (NDC depth 0..1), w=1.
inline bool shadowIntersectsWorldBounds(const glm::mat4& lightSpace,
                                       const glm::vec3& low, const glm::vec3& high) {
    return ShadowBoundsTester(lightSpace).intersects(low, high);
}
}
