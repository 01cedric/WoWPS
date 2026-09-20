#pragma once
#include <algorithm>
#include <cmath>
namespace wowee::rendering {
inline constexpr bool kDefaultBloomEnabled = true;
inline constexpr float kDefaultBloomIntensity = 0.25f;
inline float clampBloomIntensity(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : kDefaultBloomIntensity;
}
}
