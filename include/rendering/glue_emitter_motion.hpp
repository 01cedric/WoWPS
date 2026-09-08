#pragma once

#include "rendering/m2_track_sampler.hpp"
#include <random>

namespace wowee::rendering::glue {

// Authored planar emitters distribute snow across an area. Emitting every
// flake at the bone origin collapses the sheet into one overlapping streak.
template<class RandomEngine>
inline bool planeParticleMotion(const pipeline::M2ParticleEmitter& emitter,
        int sequence, float timeMs, float globalTimeMs,
        const std::vector<uint32_t>& globalDurations, const glm::mat4& transform,
        RandomEngine& random, glm::vec3& position, glm::vec3& velocity) {
    auto sample = [&](const pipeline::M2AnimationTrack& track) {
        return m2_track::sampleFloat(track, sequence, timeMs, globalTimeMs,
                                    globalDurations, 0.0f);
    };
    const float width = sample(emitter.emissionAreaWidth);
    const float length = sample(emitter.emissionAreaLength);
    const float speed = sample(emitter.emissionSpeed);
    const float variation = sample(emitter.speedVariation);
    const float vertical = sample(emitter.verticalRange);
    const float horizontal = sample(emitter.horizontalRange);
    for (float value : {width, length, speed, variation, vertical, horizontal})
        if (!std::isfinite(value)) return false;
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const glm::vec3 offset(unit(random) * std::max(width, 0.0f) * .5f,
                           unit(random) * std::max(length, 0.0f) * .5f, 0);
    const float polar = unit(random) * vertical;
    const float azimuth = unit(random) * horizontal;
    const float variedSpeed = speed * (1.0f + unit(random) * variation);
    const glm::vec3 direction(std::sin(polar) * std::cos(azimuth),
                              std::sin(polar) * std::sin(azimuth), std::cos(polar));
    position = glm::vec3(transform * glm::vec4(emitter.position + offset, 1));
    velocity = glm::mat3(transform) * direction * variedSpeed;
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z);
}

} // namespace wowee::rendering::glue
