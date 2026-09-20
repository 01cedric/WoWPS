#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wowee::rendering {
struct EnvironmentWeather {
    uint32_t type = 0;
    float intensity = 0.0f;
    bool usesOvercastLighting() const {
        return type != 0 && intensity > 0.05f;
    }
};

// Resolve the forecast once. Particle quality/indoor suppression is separate:
// a camera moving under a roof must not turn the outdoor weather into clear sky.
inline EnvironmentWeather resolveEnvironmentWeather(bool serverAuthority,
    uint32_t serverType, float serverIntensity, uint32_t zoneType, float zoneIntensity) {
    EnvironmentWeather result;
    result.type = serverAuthority ? serverType : zoneType;
    const float intensity = serverAuthority ? serverIntensity : zoneIntensity;
    if (result.type > 3 || !std::isfinite(intensity)) return {};
    result.intensity = result.type == 0 ? 0.0f : std::clamp(intensity, 0.0f, 1.0f);
    return result;
}
} // namespace wowee::rendering
