#pragma once

/**
 * Graphics settings shared by the CVar and settings-panel interfaces.
 * Renderer reads stored values at startup, then installs these live sinks.
 * Shadow scope changes live; shadow-map resolution changes at next startup.
 * Volumetric quality changes live. Its resource allocation and destruction are
 * deferred to the post-process resource boundary, which waits for in-flight
 * work before releasing images. Low/high changes only update the sample count.
 * Renderer clears the sinks at shutdown; callers tolerate an unset sink.
 */

#include <functional>

namespace wowee {
namespace rendering {

// New PS4 profiles include the requested low-cost shafts. Explicit saved Off
// remains Off. Desktop retains its previous opt-in default.
#ifdef WOWEE_PS4
inline constexpr int kDefaultVolumetricQuality = 1;
inline constexpr const char* kVolumetricQualityDefaultCVar = "1";
#else
inline constexpr int kDefaultVolumetricQuality = 0;
inline constexpr const char* kVolumetricQualityDefaultCVar = "0";
#endif

struct RenderSettingSinks {
    /// extShadowQuality: 0 nothing casts, 1 terrain only, 2+ everything.
    /// Only the scope arrives live; the map's resolution is fixed at start-up
    /// because its images are, which is why the panel marks it as needing a
    /// restart. See Renderer::setShadowQuality.
    std::function<void(int)> setShadowQuality;

    /// Shadowed sun / moon rays: 0 off, 1 low, 2 high. Applied live.
    std::function<void(int)> setVolumetricQuality;
    std::function<void(int)> setVolumetricDebug;
    std::function<void(float)> setVolumetricIntensity;
    std::function<void(float)> setVolumetricFogIntensity;
    std::function<void(bool)> setVolumetricRaysEnabled;
    std::function<void(bool)> setVolumetricFogEnabled;
    std::function<void(bool)> setBloomEnabled;
    std::function<void(float)> setBloomIntensity;

};

/// The live set. Filled by Renderer::initialize and cleared by its shutdown,
/// so a CVar written after the renderer is gone finds nothing rather than a
/// dangling capture.
inline RenderSettingSinks& renderSettingSinks() {
    static RenderSettingSinks sinks;
    return sinks;
}

}  // namespace rendering
}  // namespace wowee
