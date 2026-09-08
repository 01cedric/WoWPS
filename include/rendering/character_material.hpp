#pragma once

#include "rendering/m2_track_sampler.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cstdint>

namespace wowee::rendering {

// Shared with character.frag.glsl (std140). Keep each UV row on a 16-byte
// boundary, including builds where GLM's vector types have scalar alignment.
struct CharMaterialUBO {
    float opacity;
    int32_t alphaTest;
    int32_t colorKeyBlack;
    int32_t unlit;
    float emissiveBoost;
    float emissiveTintR, emissiveTintG, emissiveTintB;
    float specularIntensity;
    int32_t enableNormalMap;
    int32_t enablePOM;
    float pomScale;
    int32_t pomMaxSamples;
    float heightMapVariance;
    float normalMapStrength;
    int32_t hairMaterial;
    glm::vec4 uvRow0{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 uvRow1{0.0f, 1.0f, 0.0f, 0.0f};
};
static_assert(offsetof(CharMaterialUBO, uvRow0) == 64);
static_assert(offsetof(CharMaterialUBO, uvRow1) == 80);
static_assert(sizeof(CharMaterialUBO) == 96);

inline void sampleCharacterMaterialUV(CharMaterialUBO& material,
        const pipeline::M2Model& model, const pipeline::M2Batch& batch,
        int sequence, float timeMs, float globalTimeMs) {
    material.uvRow0 = {1,0,0,0};
    material.uvRow1 = {0,1,0,0};
    // A batch indexes the transform lookup table, not the transforms directly.
    if (batch.textureAnimIndex == 0xffffu ||
        batch.textureAnimIndex >= model.textureTransformLookup.size()) return;
    const uint16_t index = model.textureTransformLookup[batch.textureAnimIndex];
    if (index >= model.textureTransforms.size()) return;
    const auto& transform = model.textureTransforms[index];
    const auto translation = m2_track::sampleVec3(transform.translation, sequence,
        timeMs, globalTimeMs, model.globalSequenceDurations, glm::vec3(0));
    const auto rotation = m2_track::sampleQuat(transform.rotation, sequence,
        timeMs, globalTimeMs, model.globalSequenceDurations);
    const auto scale = m2_track::sampleVec3(transform.scale, sequence,
        timeMs, globalTimeMs, model.globalSequenceDurations, glm::vec3(1));
    // M2 transforms operate around the texture centre. Preserve unbounded
    // translation: the authored sampler's wrap flags handle scrolling.
    const glm::mat4 matrix = glm::translate(glm::mat4(1), translation) *
        glm::translate(glm::mat4(1), glm::vec3(.5f, .5f, 0)) *
        glm::mat4_cast(rotation) * glm::scale(glm::mat4(1), scale) *
        glm::translate(glm::mat4(1), glm::vec3(-.5f, -.5f, 0));
    material.uvRow0 = {matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]};
    material.uvRow1 = {matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]};
}

} // namespace wowee::rendering
