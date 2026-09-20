#pragma once
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace wowee::rendering {
// Bounds from the actual index range, not MOGP/MOBA authoring boxes. These
// remain in model space so one inverse-transformed frustum works for every
// group/range in an instance, including rotated or reflected placements.
struct WmoDrawBounds {
    glm::vec3 low{0.0f};
    glm::vec3 high{0.0f};
    bool valid = false;
};

template<class Vertices, class Indices>
WmoDrawBounds wmoDrawBounds(const Vertices& vertices, const Indices& indices,
                            uint32_t first, uint32_t count) {
    WmoDrawBounds result;
    if (!count || first > indices.size() || count > indices.size() - first)
        return result;
    for (size_t i = first; i < size_t(first) + count; ++i) {
        if (indices[i] >= vertices.size()) return {};
        const glm::vec3 p = vertices[indices[i]];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            return {};
        if (!result.valid) {
            result.low = result.high = p;
            result.valid = true;
        } else {
            result.low = glm::min(result.low, p);
            result.high = glm::max(result.high, p);
        }
    }
    // WMO vertices are static (lava animates UVs only). A small padding keeps
    // edge contact conservative under CPU/GPU matrix rounding differences.
    result.low -= glm::vec3(0.5f);
    result.high += glm::vec3(0.5f);
    return result;
}
}
