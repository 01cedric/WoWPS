#pragma once

/// Bounds for a model, computed one way.
///
/// A .wom carries a box and a radius, and they are read by the shadow cascade
/// margin, the character renderer and the editor viewport's framing. Eighteen
/// places wrote them, in three different ways:
///
///   * half the box diagonal, in fifteen
///   * the furthest vertex from the box centre, in two
///   * the furthest vertex from the model origin, in one
///
/// The first two are spheres around the box centre. The field is not that.
/// M2Loader sets `boundRadius` from the M2 header's own boundingRadius, which
/// is measured from the model origin, and WoweeBuildingLoader::fromWMO matches
/// it. A generator that writes a box-centred radius is spelling a different
/// fact into the same field, and the two only agree for a model that happens
/// to sit centred on its origin.
///
/// Measured over the editor's ten mesh generators, three - the stairs, the
/// ramp and the cone - produced a radius smaller than the model's own extent
/// from the origin, the stairs by nearly half.
///
/// Nothing here reports an error. A radius that is too small under-margins the
/// shadow cascade and frames the model too tightly in the viewport; one that is
/// too large costs culling that would otherwise happen.

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace wowee::pipeline {

/// An axis-aligned box and the radius of a sphere at the model origin.
struct ModelBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    float radius = 0.0f;
};

/// The bounds of a set of positions.
///
/// `radius` is the distance to the furthest one from the origin, which is
/// where the M2 header measures from and where anything that draws a sphere
/// for this model centres it.
///
/// An empty model gets a zero box and a zero radius rather than the inverted
/// box a min/max seed would leave, because that inverted box reaches every
/// consumer as a real one.
template <typename PositionOf, typename Vertices>
ModelBounds modelBoundsOf(const Vertices& vertices, PositionOf positionOf) {
    ModelBounds out;
    bool first = true;
    float furthestSq = 0.0f;
    for (const auto& vertex : vertices) {
        const glm::vec3 p = positionOf(vertex);
        if (first) {
            out.min = p;
            out.max = p;
            first = false;
        } else {
            out.min = glm::min(out.min, p);
            out.max = glm::max(out.max, p);
        }
        furthestSq = glm::max(furthestSq, glm::dot(p, p));
    }
    if (first) return out;
    out.radius = glm::sqrt(furthestSq);
    return out;
}

/// The bounds of a container of plain positions.
inline ModelBounds modelBounds(const std::vector<glm::vec3>& positions) {
    return modelBoundsOf(positions, [](const glm::vec3& p) { return p; });
}

}  // namespace wowee::pipeline
