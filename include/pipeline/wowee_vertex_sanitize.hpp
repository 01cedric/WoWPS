#pragma once

/**
 * wowee_vertex_sanitize.hpp - what a vertex becomes when its floats are not
 * numbers.
 *
 * A NaN or an infinity in a vertex is not a bad-looking triangle. It is a
 * matrix that stops being invertible, a draw whose bounds compare false against
 * everything, and on some drivers a device lost with no message - the M2 vertex
 * shader takes one and the whole pipeline goes. So both formats that carry
 * vertices scrub them, on the way in and on the way out.
 *
 * The scrubbing was written three times: reading a building, writing a
 * building, and reading a model. What makes that worth collapsing is not the
 * loop but the replacements, which are not all zero and cannot be guessed:
 *
 *   position   0, 0, 0     - the origin is somewhere; it is at least drawable
 *   normal     0, 0, 1     - a zeroed normal is not a direction at all, and
 *                            lighting divides by its length
 *   texCoord   0, 0
 *   colour     1, 1, 1, 1  - a zeroed colour is transparent black, which hides
 *                            the triangle instead of repairing it
 *
 * Two of those are the interesting ones, and they are the two a fourth copy
 * would most likely get wrong.
 */

#include <cmath>

namespace wowee {
namespace pipeline {

/// Replace every non-finite float in `v` with something a renderer can use.
///
/// Works for a vertex with a colour and one without: the model format's vertex
/// carries bone weights where the building format's carries a colour, and only
/// the one that has it gets that part.
template <typename Vertex>
void sanitizeVertex(Vertex& v) {
    if (!std::isfinite(v.position.x)) v.position.x = 0.0f;
    if (!std::isfinite(v.position.y)) v.position.y = 0.0f;
    if (!std::isfinite(v.position.z)) v.position.z = 0.0f;
    if (!std::isfinite(v.normal.x)) v.normal.x = 0.0f;
    if (!std::isfinite(v.normal.y)) v.normal.y = 0.0f;
    // Not zero: a normal of (0,0,0) has no direction and no length to divide by.
    if (!std::isfinite(v.normal.z)) v.normal.z = 1.0f;
    if (!std::isfinite(v.texCoord.x)) v.texCoord.x = 0.0f;
    if (!std::isfinite(v.texCoord.y)) v.texCoord.y = 0.0f;
    if constexpr (requires { v.color[0]; }) {
        // Opaque white, so a repaired vertex is visible rather than invisible.
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(v.color[c])) v.color[c] = 1.0f;
        }
    }
}

/// The same over a whole container of them.
template <typename Vertices>
void sanitizeVertices(Vertices& vertices) {
    for (auto& v : vertices) sanitizeVertex(v);
}

}  // namespace pipeline
}  // namespace wowee
