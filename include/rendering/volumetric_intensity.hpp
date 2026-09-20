#pragma once
#include <algorithm>
#include <cmath>
namespace wowee::rendering {
inline constexpr float kDefaultVolumetricIntensity = 1.35f;
// halve sun/moon shaft intensity while retaining the saved slider.
// Artistic exposure of the shadowed scattering term only. Keep the persisted
// control in its original range so zero/custom user values survive upgrades.
inline constexpr float kVolumetricDisplayExposure = 0.75f;
inline constexpr float kVolumetricExtinction = 0.002f;
inline float clampVolumetricIntensity(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 2.0f) : kDefaultVolumetricIntensity;
}
inline float effectiveVolumetricIntensity(float value) {
    return clampVolumetricIntensity(value) * kVolumetricDisplayExposure;
}
}
