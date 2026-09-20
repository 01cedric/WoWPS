#include "rendering/m2_shadow.hpp"
#include "rendering/shadow_ranges.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>

using namespace wowee::rendering;
using wowee::pipeline::M2TextureTransform;

static bool near(glm::vec4 a, glm::vec4 b) {
    return glm::all(glm::lessThanEqual(glm::abs(a - b),
        glm::vec4(0.0003f) + glm::abs(b) * 0.00003f));
}
static glm::vec4 shaderAffine(const glm::vec4 (&rows)[3], glm::vec4 point) {
    return {glm::dot(rows[0], point), glm::dot(rows[1], point), glm::dot(rows[2], point), 1};
}
struct FixtureModel {
    struct Batch { uint16_t textureAnimIndex = 0xFFFF; };
    std::vector<Batch> batches{{0}};
    bool hasTextureAnimation = true;
    bool isLavaModel = false;
    std::vector<uint16_t> textureTransformLookup{0};
    std::vector<M2TextureTransform> textureTransforms{1};
    std::vector<uint32_t> globalSequenceDurations{1000};
};
struct FixtureBatch { uint32_t materialBatch = 0, maskMode = 1; };
struct FixtureInstance {
    int currentSequenceIndex = 0;
    float animTime = 250;
    float globalSequenceTime = 750;
};

int main() {
    for (uint16_t blend = 0; blend < 8; ++blend) {
        assert(m2ShadowBatchCasts(blend, 1, false, false) == (blend <= 1));
        assert(!m2ShadowBatchCasts(blend, 0.009f, false, false));
        assert(!m2ShadowBatchCasts(blend, 1, true, false));
        assert(!m2ShadowBatchCasts(blend, 1, false, true));
    }
    assert(m2ShadowBatchCasts(1, 0.01f, false, false));
    assert(m2ShadowMaskMode(0, false, false, false) == 0);
    assert(m2ShadowMaskMode(1, false, false, false) == 1);
    assert(m2ShadowMaskMode(0, true, false, false) == 2);
    assert(m2ShadowMaskMode(1, true, true, false) == 3);
    assert(m2ShadowMaskMode(0, false, false, true) == 5);
    assert(!m2ShadowNeedsPerInstanceUv(0, true, 0));
    assert(!m2ShadowNeedsPerInstanceUv(1, false, 0));
    assert(!m2ShadowNeedsPerInstanceUv(1, true, 0xFFFF));
    assert(m2ShadowNeedsPerInstanceUv(1, true, 0));
    std::vector<ShadowRange> opaque{{0, 6}, {0, 6}, {6, 6}, {30, 3}};
    coalesceShadowRanges(opaque);
    assert(opaque.size() == 2 && opaque[0].firstIndex == 0 && opaque[0].indexCount == 12);

    // The actual packed rows consumed by both GLSL variants reproduce the
    // original full model + orthographic-light multiplication, including
    // reflected/nonuniform scales, rotations and large translated placements.
    std::mt19937 rng(258);
    std::uniform_real_distribution<float> random(-100, 100);
    for (unsigned i = 0; i < 1000; ++i) {
        glm::mat4 model = glm::translate(glm::mat4(1),
            glm::vec3(random(rng), random(rng), random(rng)));
        model = glm::rotate(model, random(rng), glm::normalize(glm::vec3(1, 2, 3)));
        model = glm::scale(model, glm::vec3((i & 1) ? -1.3f : 0.7f, 2.5f, 0.2f));
        const glm::mat4 light = glm::ortho(-70.f, 70.f, -60.f, 60.f, 1.f, 1000.f) *
            glm::lookAt(glm::vec3(120, -200, 400), glm::vec3(4, 9, 0), glm::vec3(0, 0, 1));
        M2ShadowPush push{};
        M2ShadowInstancedPush instanced{};
        m2ShadowAffineRows(model, push.modelRows);
        m2ShadowAffineRows(light, push.lightRows);
        m2ShadowAffineRows(light, instanced.lightRows);
        glm::vec4 point(random(rng), random(rng), random(rng), 1);
        const auto world = shaderAffine(push.modelRows, point);
        assert(near(world, model * point));
        assert(near(shaderAffine(push.lightRows, world), light * model * point));
        assert(near(shaderAffine(instanced.lightRows, model * point), light * model * point));
    }

    FixtureModel model;
    FixtureBatch batch;
    FixtureInstance instance;
    auto& transform = model.textureTransforms[0];
    transform.translation.interpolationType = 1;
    transform.translation.sequences.resize(1);
    transform.translation.sequences[0].timestamps = {0, 1000};
    transform.translation.sequences[0].vec3Values = {{0, 0, 0}, {1, 2, 0}};
    transform.rotation.sequences.resize(1);
    transform.rotation.sequences[0].timestamps = {0};
    transform.rotation.sequences[0].quatValues = {glm::angleAxis(0.7f, glm::vec3(0, 0, 1))};
    transform.scale.sequences.resize(1);
    transform.scale.sequences[0].timestamps = {0};
    transform.scale.sequences[0].vec3Values = {{2, 0.5f, 1}};
    auto uv = sampleM2ShadowUv(model, batch, instance, 5.f);
    auto reference = sampleM2Uv(&transform, 0, 250, 750, model.globalSequenceDurations);
    assert(uv.offset == reference.offset && uv.linear == reference.linear);
    instance.animTime = 750;
    const auto second = sampleM2ShadowUv(model, batch, instance, 5.f);
    assert(second.offset != uv.offset && second.linear == uv.linear);
    transform.translation.globalSequence = 0;
    instance.globalSequenceTime = 1250;
    reference = sampleM2Uv(&transform, 0, 750, 1250, model.globalSequenceDurations);
    uv = sampleM2ShadowUv(model, batch, instance, 5.f);
    assert(uv.offset == reference.offset && uv.linear == reference.linear);
    // Invalid authored lookups and no-transform sentinels match scene identity.
    model.textureTransformLookup[0] = 100;
    uv = sampleM2ShadowUv(model, batch, instance, 5.f);
    assert(uv.offset == glm::vec2(0) && uv.linear == glm::vec4(1, 0, 0, 1));
    model.textureTransformLookup[0] = 0;
    model.batches[0].textureAnimIndex = 0xFFFF;
    uv = sampleM2ShadowUv(model, batch, instance, 5.f);
    assert(uv.offset == glm::vec2(0));
    model.isLavaModel = true;
    uv = sampleM2ShadowUv(model, batch, instance, 5.f);
    assert(uv.offset == glm::vec2(5.f * .03f, -5.f * .08f));
    batch.maskMode = 0;
    uv = sampleM2ShadowUv(model, batch, instance, 5.f);
    assert(uv.offset == glm::vec2(0)); // opaque groups remain instanced/merged
    std::cout << "PASS M2 binary caster policy, mask classes, opaque merging, 128-byte affine ABI, "
                 "ordinary/global animated UVs, fallback lookups and static/animated submission\n";
}
