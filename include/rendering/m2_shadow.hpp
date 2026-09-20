#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include "rendering/m2_texture_transform.hpp"

namespace wowee::rendering {

// Binary shadow policy: solid and alpha-key surfaces occlude; a blended or
// emissive overlay does not turn its card into a solid sun blocker.
inline bool m2ShadowBatchCasts(uint16_t blendMode, float opacity,
                              bool spellEffect, bool forgeFireCard) {
    return blendMode <= 1 && opacity >= 0.01f && !spellEffect && !forgeFireCard;
}

// Low two bits encode the scene pass's alpha cutoff class; bit 2 retains its
// opaque-pass black key. Texture alpha alone never makes a rigid opaque
// material a cutout. Foliage/ground use the same force-cutout rule as scene.
inline uint32_t m2ShadowMaskMode(uint16_t blendMode, bool foliageLike,
                                 bool groundDetail, bool colorKeyBlack) {
    uint32_t mode = groundDetail ? 3u : foliageLike ? 2u :
        (blendMode == 1 || colorKeyBlack) ? 1u : 0u;
    return mode | (colorKeyBlack ? 4u : 0u);
}

inline bool m2ShadowNeedsPerInstanceUv(uint32_t maskMode, bool hasTextureAnimation,
                                       uint16_t textureAnimIndex) {
    return maskMode != 0 && hasTextureAnimation && textureAnimIndex != 0xFFFF;
}

// Emit contiguous runs only: matrices stay in their immutable uploaded order.
// Equality covers all six UV components with no quantization. Different clocks
// may produce the same transform, while equal animation flags prove nothing.
// The caller scopes each invocation to one model and one material batch, so
// geometry, mask, UV channel and descriptor are already identical. Per-instance
// placement and wind remain in the shader's matrix lookup.
template<class Sample, class Emit>
void forEachM2ShadowUvRun(size_t begin, size_t end, bool varyingUv,
                         Sample&& sample, Emit&& emit) {
    if (begin == end) return;
    size_t runBegin = begin;
    auto runUv = sample(begin);
    if (varyingUv) {
        for (size_t index = begin + 1; index < end; ++index) {
            const auto uv = sample(index);
            if (uv.linear == runUv.linear && uv.offset == runUv.offset) continue;
            emit(runBegin, index - runBegin, runUv);
            runBegin = index;
            runUv = uv;
        }
    }
    emit(runBegin, end - runBegin, runUv);
}

// Use the same authored transform lookup and animation clocks as the scene.
// The template accepts the renderer's existing model/batch/instance records.
template<class Model, class ShadowBatch, class Instance>
M2UvTransform sampleM2ShadowUv(const Model& model, const ShadowBatch& shadow,
                              const Instance& instance, float lavaAnimSeconds) {
    M2UvTransform uv;
    if (shadow.maskMode == 0) return uv;
    const auto& batch = model.batches[shadow.materialBatch];
    const pipeline::M2TextureTransform* transform = nullptr;
    if (model.hasTextureAnimation && batch.textureAnimIndex != 0xFFFF &&
        batch.textureAnimIndex < model.textureTransformLookup.size()) {
        const uint16_t index = model.textureTransformLookup[batch.textureAnimIndex];
        if (index < model.textureTransforms.size()) transform = &model.textureTransforms[index];
    }
    uv = sampleM2Uv(transform, instance.currentSequenceIndex, instance.animTime,
        instance.globalSequenceTime, model.globalSequenceDurations);
    if (model.isLavaModel && uv.offset == glm::vec2(0.0f))
        uv.offset = glm::vec2(lavaAnimSeconds * 0.03f, -lavaAnimSeconds * 0.08f);
    return uv;
}

// Orthographic light and affine placement matrices have a constant last row
// (0,0,0,1). Explicit row vectors save 32 bytes, leaving space for the full
// authored UV transform while staying inside Vulkan's 128-byte minimum.
struct M2ShadowPush {
    glm::vec4 lightRows[3];
    glm::vec4 modelRows[3];
    glm::vec4 uvLinear{1, 0, 0, 1};
    glm::vec2 uvOffset{0};
    uint32_t texCoordSet = 0;
    uint32_t maskMode = 0;
};
struct M2ShadowInstancedPush {
    glm::vec4 lightRows[3];
    uint32_t instanceDataOffset = 0;
    uint32_t reserved[11] = {};
    glm::vec4 uvLinear{1, 0, 0, 1};
    glm::vec2 uvOffset{0};
    uint32_t texCoordSet = 0;
    uint32_t maskMode = 0;
};
static_assert(sizeof(M2ShadowPush) == 128);
static_assert(offsetof(M2ShadowPush, lightRows) == 0);
static_assert(offsetof(M2ShadowPush, modelRows) == 48);
static_assert(offsetof(M2ShadowPush, uvLinear) == 96);
static_assert(offsetof(M2ShadowPush, uvOffset) == 112);
static_assert(offsetof(M2ShadowPush, texCoordSet) == 120);
static_assert(offsetof(M2ShadowPush, maskMode) == 124);
static_assert(sizeof(M2ShadowInstancedPush) == 128);
static_assert(offsetof(M2ShadowInstancedPush, lightRows) == 0);
static_assert(offsetof(M2ShadowInstancedPush, instanceDataOffset) == 48);
static_assert(offsetof(M2ShadowInstancedPush, reserved) == 52);
static_assert(offsetof(M2ShadowInstancedPush, uvLinear) == 96);
static_assert(offsetof(M2ShadowInstancedPush, uvOffset) == 112);
static_assert(offsetof(M2ShadowInstancedPush, texCoordSet) == 120);
static_assert(offsetof(M2ShadowInstancedPush, maskMode) == 124);

inline void m2ShadowAffineRows(const glm::mat4& matrix, glm::vec4 (&rows)[3]) {
    for (unsigned row = 0; row < 3; ++row)
        rows[row] = {matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]};
}

} // namespace wowee::rendering
