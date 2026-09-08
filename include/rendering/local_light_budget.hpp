#pragma once
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

namespace wowee::rendering {
// In-place bounded selection keeps position/radius and colour/intensity paired.
// The input arrays already belong to the per-frame UBO; no temporary allocation.
inline uint32_t nearestLocalLights(const glm::vec3& camera, glm::vec4* positions,
                                   glm::vec4* colours, uint32_t count, uint32_t limit) {
    const auto keep = std::min(count, limit);
    for (uint32_t i = 0; i < keep; ++i) {
        uint32_t best = i;
        auto distance = [&](uint32_t n) {
            const glm::vec3 delta = glm::vec3(positions[n]) - camera;
            return glm::dot(delta, delta);
        };
        for (uint32_t j = i + 1; j < count; ++j)
            if (distance(j) < distance(best)) best = j;
        if (best != i) {
            std::swap(positions[i], positions[best]);
            std::swap(colours[i], colours[best]);
        }
    }
    return keep;
}
} // namespace wowee::rendering
