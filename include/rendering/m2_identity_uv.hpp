#pragma once
#include "pipeline/m2_loader.hpp"
#include <algorithm>

namespace wowee::rendering {
// Prove that EVERY selectable key and sequence produces the sampler defaults.
// Empty/unavailable sequences also return these defaults. No clock quantization
// or guessed animation flag: any nonidentity authored component keeps sampling.
inline bool m2TextureTransformIsIdentity(const pipeline::M2TextureTransform& transform) {
    const auto vecIsDefault = [](const auto& track, const glm::vec3& fallback) {
        for (const auto& keys : track.sequences) {
            const size_t count = std::min(keys.timestamps.size(), keys.vec3Values.size());
            for (size_t i = 0; i < count; ++i)
                if (keys.vec3Values[i] != fallback) return false;
        }
        return true;
    };
    if (!vecIsDefault(transform.translation, glm::vec3(0.0f)) ||
        !vecIsDefault(transform.scale, glm::vec3(1.0f))) return false;
    for (const auto& keys : transform.rotation.sequences) {
        const size_t count = std::min(keys.timestamps.size(), keys.quatValues.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& q = keys.quatValues[i];
            if (q.x != 0.0f || q.y != 0.0f || q.z != 0.0f ||
                (q.w != 1.0f && q.w != -1.0f)) return false;
        }
    }
    return true;
}
}
