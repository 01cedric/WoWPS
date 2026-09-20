#pragma once

namespace wowee::rendering {
struct VolumetricEligibility {
    bool allowed;
    const char* reason;
};

// A WMO's indoor classification is not a light-visibility query: windows,
// doorways and city group bounds can all include camera-visible outdoor air.
// The ray's scene-depth endpoint and directional shadow map decide visibility.
// Keep underwater and unavailable-shadow guards; do not fabricate unshadowed
// shafts when the directional depth image cannot be sampled.
inline constexpr VolumetricEligibility volumetricEligibility(bool submerged,
    bool shadowsEnabled, int shadowQuality, bool shadowDepthReady) {
    if (submerged) return {false, "camera-underwater"};
    if (!shadowsEnabled || shadowQuality <= 0) return {false, "shadows-off"};
    if (!shadowDepthReady) return {false, "shadow-not-ready"};
    return {true, "ready"};
}
} // namespace wowee::rendering
