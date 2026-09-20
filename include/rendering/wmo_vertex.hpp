#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

#include "rendering/vertex_layout.hpp"

namespace wowee {
namespace rendering {

/// One vertex of WMO geometry, as the buffers are actually filled.
///
/// This layout was written out four times in wmo_renderer.cpp: once by the
/// code that fills the buffer, twice by the two functions that build the
/// pipelines, and once more by the shadow pipeline, which spelled the stride
/// and every offset as a literal. Adding a field would have moved the real
/// offsets and left that fourth copy reading the old ones, with shadows drawn
/// from whatever the bytes happened to be.
struct WMOVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 color;
    /// xyz is the tangent direction, w the handedness, plus or minus one.
    glm::vec4 tangent;
};

/// What assets/shaders/wmo.vert.glsl declares, in its order.
inline constexpr std::array<VertexAttribute, 5> kWmoVertexAttributes = {{
    {.location = 0, .componentCount = 3, .offset = static_cast<uint32_t>(offsetof(WMOVertex, position))},
    {.location = 1, .componentCount = 3, .offset = static_cast<uint32_t>(offsetof(WMOVertex, normal))},
    {.location = 2, .componentCount = 2, .offset = static_cast<uint32_t>(offsetof(WMOVertex, texCoord))},
    {.location = 3, .componentCount = 4, .offset = static_cast<uint32_t>(offsetof(WMOVertex, color))},
    {.location = 4, .componentCount = 4, .offset = static_cast<uint32_t>(offsetof(WMOVertex, tangent))},
}};

/// Dedicated WMO shadow geometry uses the same position and authored UV.
inline constexpr std::array<VertexAttribute, 2> kWmoShadowVertexAttributes = {{
    {.location = 0, .componentCount = 3, .offset = static_cast<uint32_t>(offsetof(WMOVertex, position))},
    {.location = 1, .componentCount = 2, .offset = static_cast<uint32_t>(offsetof(WMOVertex, texCoord))},
}};

}  // namespace rendering
}  // namespace wowee
