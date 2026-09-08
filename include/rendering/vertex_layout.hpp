#pragma once

#include <cstddef>
#include <cstdint>

namespace wowee {
namespace rendering {

/// One vertex attribute, described without reference to Vulkan.
///
/// A vertex layout is agreed between three parties that cannot check each
/// other: the struct the renderer fills, the attribute descriptions handed to
/// the pipeline, and the `layout(location = N) in` lines of the shader. When
/// they disagree nothing fails to build and nothing raises. The geometry draws
/// with its fields read from the wrong offsets, which looks like a model
/// exploding, or a surface lit from the wrong direction, or shadows in the
/// wrong place.
///
/// Keeping the description as plain data rather than as
/// VkVertexInputAttributeDescription is what lets a test compare it against the
/// shader source, which is the only independent statement of the same fact in
/// the tree.
struct VertexAttribute {
    uint32_t location = 0;
    /// Floats in the attribute: 2 for a vec2, 3 for a vec3, 4 for a vec4.
    uint32_t componentCount = 0;
    /// Bytes from the start of the vertex.
    uint32_t offset = 0;
};

}  // namespace rendering
}  // namespace wowee
