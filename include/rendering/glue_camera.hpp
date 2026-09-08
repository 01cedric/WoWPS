#pragma once

#include "pipeline/m2_loader.hpp"
#include <algorithm>
#include <cmath>

namespace wowee::rendering::glue {

inline bool finite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Interpolate the actual M2 key and its authored spline handles. Hermite
// handles are derivatives in the normalized interval, Bezier handles are
// control points. Step tracks must not creep between successive keys.
template<class T>
T interpolate(uint16_t kind, const T& a, const T& b,
              const T& out, const T& in, float t, bool handles) {
    if (kind == 0) return a;
    if (!handles || kind == 1) return a * (1.0f - t) + b * t;
    const float t2 = t * t, t3 = t2 * t;
    if (kind == 2) return a * (2*t3 - 3*t2 + 1) + out * (t3 - 2*t2 + t) +
                          b * (-2*t3 + 3*t2) + in * (t3 - t2);
    if (kind == 3) {
        const float u = 1.0f - t;
        return a * (u*u*u) + out * (3*u*u*t) + in * (3*u*t2) + b * t3;
    }
    return a * (1.0f - t) + b * t;
}

template<class T>
T sample(const pipeline::M2AnimationTrack& track, size_t sequence, float timeMs,
         const std::vector<T>& values, const std::vector<T>& ins,
         const std::vector<T>& outs, const T& fallback) {
    if (sequence >= track.sequences.size() || values.empty()) return fallback;
    const auto& times = track.sequences[sequence].timestamps;
    const size_t count = std::min(times.size(), values.size());
    if (count < 2 || !std::isfinite(timeMs) || timeMs <= times.front()) return values.front();
    if (timeMs >= times[count - 1]) return values[count - 1];
    // Files are normally sorted; a bounded scan also avoids relying on a
    // sorted-range precondition when a damaged archive carries bad keys.
    size_t next = 1;
    while (next + 1 < count && timeMs >= times[next]) ++next;
    const float duration = static_cast<float>(times[next]) - times[next - 1];
    const float t = duration > 0 ? std::clamp((timeMs - times[next - 1]) / duration, 0.0f, 1.0f) : 0.0f;
    const bool handles = outs.size() >= count && ins.size() >= count;
    return interpolate(track.interpolationType, values[next - 1], values[next],
                       handles ? outs[next - 1] : fallback,
                       handles ? ins[next] : fallback, t, handles);
}

inline size_t trackSequence(const pipeline::M2AnimationTrack& track, size_t sequence,
                            double clockMs, const std::vector<uint32_t>& globals, float& timeMs) {
    if (track.globalSequence >= 0) {
        sequence = 0;
        const size_t g = static_cast<size_t>(track.globalSequence);
        timeMs = g < globals.size() && globals[g] > 0
            ? static_cast<float>(std::fmod(clockMs, static_cast<double>(globals[g]))) : 0.0f;
    }
    return sequence;
}

inline glm::vec3 samplePosition(const pipeline::M2AnimationTrack& track, size_t sequence,
                               float timeMs, double clockMs, const std::vector<uint32_t>& globals) {
    sequence = trackSequence(track, sequence, clockMs, globals, timeMs);
    if (sequence >= track.sequences.size()) return glm::vec3(0);
    const auto& k = track.sequences[sequence];
    const auto value = sample(track, sequence, timeMs, k.vec3Values,
                              k.spline().vec3InTangents, k.spline().vec3OutTangents, glm::vec3(0));
    return finite(value) ? value : glm::vec3(0);
}

inline float sampleRoll(const pipeline::M2AnimationTrack& track, size_t sequence,
                        float timeMs, double clockMs, const std::vector<uint32_t>& globals) {
    sequence = trackSequence(track, sequence, clockMs, globals, timeMs);
    if (sequence >= track.sequences.size()) return 0;
    const auto& k = track.sequences[sequence];
    const float value = sample(track, sequence, timeMs, k.floatValues,
                               k.spline().floatInTangents, k.spline().floatOutTangents, 0.0f);
    return std::isfinite(value) ? value : 0;
}

// Preview UI angles are degrees; CharacterRenderer model transforms use
// GLM radians. Keep the conversion at the API boundary, including creation.
inline glm::vec3 previewModelRotation(float yawDegrees) {
    return glm::vec3(0.0f, 0.0f, glm::radians(yawDegrees));
}

inline float verticalFov(float diagonal, float aspect) {
    if (!std::isfinite(diagonal) || diagonal <= 0.05f || diagonal >= 3.0f) diagonal = 0.9f;
    if (!std::isfinite(aspect) || aspect <= 0) aspect = 16.0f/9.0f;
    return 2.0f * std::atan(std::tan(diagonal * 0.5f) / std::sqrt(1 + aspect * aspect));
}

// Magnify the authored scene about its camera axis without stretching either
// dimension or moving the camera through the foreground geometry.
inline float scaledVerticalFov(float diagonal, float aspect, float scale) {
    if (!std::isfinite(scale)) scale = 1.0f;
    scale = std::clamp(scale, 1.0f, 2.0f);
    return 2.0f * std::atan(std::tan(verticalFov(diagonal, aspect) * 0.5f) / scale);
}

} // namespace wowee::rendering::glue
