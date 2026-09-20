#pragma once
#include <algorithm>
#include <cmath>
namespace wowee::rendering {
inline constexpr float kDefaultVolumetricFogIntensity = 0.35f;
inline constexpr const char* kVolumetricFogIntensityDefaultCVar = "0.35";
inline float clampVolumetricFogIntensity(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : kDefaultVolumetricFogIntensity;
}
}
