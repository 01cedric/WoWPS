#pragma once

#include "rendering/frustum.hpp"
#include <algorithm>
#include <cmath>

namespace wowee::rendering {

// Origin-centered, animation-padded bound. The authored radius may exclude an
// offset body/attachment, so also contain the furthest local bounding-box
// corner. Frobenius norm bounds any scale/shear in an attachment transform.
// Invalid imported bounds fail open rather than making an object disappear.
inline bool modelBoundsSphere(const glm::mat4& matrix,
                              const glm::vec3& minimum, const glm::vec3& maximum,
                              float authoredRadius, glm::vec3& center, float& radius) {
    if (!std::isfinite(minimum.x) || !std::isfinite(minimum.y) ||
        !std::isfinite(minimum.z) || !std::isfinite(maximum.x) ||
        !std::isfinite(maximum.y) || !std::isfinite(maximum.z) ||
        !std::isfinite(authoredRadius)) return false;
    const glm::vec3 furthest = glm::max(glm::abs(minimum), glm::abs(maximum));
    const float localRadius = std::max(glm::length(furthest), std::abs(authoredRadius));
    const float scaleBound = std::sqrt(glm::dot(glm::vec3(matrix[0]), glm::vec3(matrix[0])) +
                                      glm::dot(glm::vec3(matrix[1]), glm::vec3(matrix[1])) +
                                      glm::dot(glm::vec3(matrix[2]), glm::vec3(matrix[2])));
    radius = std::max(4.0f, localRadius * scaleBound * 1.5f + 2.0f);
    center = glm::vec3(matrix[3]);
    if (!std::isfinite(localRadius) || !std::isfinite(scaleBound) ||
        !std::isfinite(radius) || !std::isfinite(center.x) ||
        !std::isfinite(center.y) || !std::isfinite(center.z)) return false;
    return true;
}

inline bool modelBoundsInFrustum(const Frustum& frustum, const glm::mat4& matrix,
                                 const glm::vec3& minimum, const glm::vec3& maximum,
                                 float authoredRadius) {
    glm::vec3 center;
    float radius;
    if (!modelBoundsSphere(matrix, minimum, maximum, authoredRadius, center, radius)) return true;
    return frustum.intersectsSphere(center, radius);
}

} // namespace wowee::rendering
