#pragma once

#include "rendering/sun_direction.hpp"

namespace wowee::rendering {

// Resolve the solar vector once for sky consumers. The lighting manager keeps
// this solar travel direction at night; only the key-light selector flips it.
inline glm::vec3 celestialSolarTravelDirection(glm::vec3 solarRay, float worldHours) {
    const float lengthSquared = glm::dot(solarRay, solarRay);
    if (std::isfinite(lengthSquared) && lengthSquared > 1.e-8f)
        return solarRay / std::sqrt(lengthSquared);
    const float hours = std::isfinite(worldHours) ? worldHours : 12.0f;
    return sunTravelDirection(hours / 24.0f);
}

// A directional source fades through the horizon before the opposite source
// becomes the key. This prevents an illuminated 180-degree shadow reversal.
// Atmospheric/ambient fill is intentionally unaffected.
inline float celestialKeyStrength(glm::vec3 solarRay) {
    const float lengthSquared = glm::dot(solarRay, solarRay);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.e-8f) return 0.0f;
    const float elevation = std::abs(solarRay.z) / std::sqrt(lengthSquared);
    return glm::smoothstep(0.0f, 0.12f, elevation);
}

inline float celestialDiscStrength(glm::vec3 solarRay, bool moon) {
    const float elevation = moon ? solarRay.z : -solarRay.z;
    return elevation > 0.0f ? celestialKeyStrength(solarRay) : 0.0f;
}

// An authored sun color already includes zone/time policy. Adding a fixed warm
// tint here would turn the deliberately cool Tirisfal disc yellow again.
inline glm::vec3 celestialDiscColor(glm::vec3 authoredColor) {
    return glm::max(authoredColor, glm::vec3(0.0f));
}

// Art-directed key chromaticity only: authored zone/time luminance remains
// unchanged. This never recolors ambient, terrain albedo, fog or local lamps.
inline glm::vec3 celestialKeyColor(glm::vec3 authoredColor, glm::vec3 solarRay,
                                   bool permanentlyCool = false) {
    authoredColor = glm::max(authoredColor,glm::vec3(0.0f));
    const glm::vec3 weights(0.2126f,0.7152f,0.0722f);
    const float luminance = glm::dot(authoredColor,weights);
    if (!(luminance > 0.0f) || !std::isfinite(luminance)) return glm::vec3(0.0f);
    const float lengthSquared = glm::dot(solarRay,solarRay);
    const float elevation = std::isfinite(lengthSquared) && lengthSquared > 1.e-8f
        ? solarRay.z/std::sqrt(lengthSquared) : -1.0f;
    const float night = permanentlyCool ? 1.0f : glm::smoothstep(-0.16f,0.16f,elevation);
    const glm::vec3 tint = glm::mix(glm::vec3(1.0f,0.63f,0.32f),
                                   glm::vec3(0.42f,0.65f,1.0f),night);
    // Retain some authored hue variation while making the active source
    // recognizably warm by day and cool by night. Normalize both chromaticities
    // before mixing so the zone cannot acquire an artificial brightness floor.
    const glm::vec3 chroma = glm::mix(authoredColor/luminance,
                                     tint/glm::dot(tint,weights),0.85f);
    return chroma*luminance;
}

// This is the existing optional decorative animation, not a calendar phase.
// Hours since midnight contain no date and cannot identify a lunar cycle.
inline float animatedMoonPhase(float initialPhase, float elapsedSeconds, float cycleSeconds) {
    if (!std::isfinite(elapsedSeconds) || cycleSeconds <= 0.0f) return initialPhase;
    const float phase = std::fmod(initialPhase + elapsedSeconds / cycleSeconds, 1.0f);
    return phase < 0.0f ? phase + 1.0f : phase;
}

} // namespace wowee::rendering
