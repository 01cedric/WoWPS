#pragma once

/// Filling the sky's parameters from what the lighting manager resolved.
///
/// The renderer records a frame two ways - secondary command buffers on worker
/// threads, or inline on one - and each path built this by hand. Fifteen
/// assignments, written twice, forty lines apart.
///
/// Nothing about a field missing from one copy fails. The sky simply looks
/// different depending on whether parallel recording is on, which is a setting
/// most people never touch and nobody would think to bisect against. The last
/// three fields were added to both copies by hand, and the fourth would have
/// been too.

#include "rendering/lighting_manager.hpp"
#include "rendering/sky_system.hpp"

namespace wowee::rendering {

/// The sky parameters for this frame.
///
/// `lighting` is null when there is no lighting manager, in which case the
/// colours keep the defaults SkyParams declares rather than being zeroed.
/// `gameTime` is the server's hour, or -1 when it has not sent one.
inline SkyParams skyParamsFromLighting(float timeOfDay, float gameTime,
                                       float weatherIntensity,
                                       const LightingParams* lighting,
                                       bool useOriginalSkybox, uint32_t skyboxFlags = 0) {
    SkyParams params;
    params.timeOfDay = timeOfDay;
    params.gameTime = gameTime;
    if (lighting) {
        params.directionalDir = lighting->directionalDir;
        params.sunColor = lighting->sunColor;
        params.cloudColor = lighting->cloudColor;
        params.skyTopColor = lighting->skyTopColor;
        params.skyMiddleColor = lighting->skyMiddleColor;
        params.skyBand1Color = lighting->skyBand1Color;
        params.skyBand2Color = lighting->skyBand2Color;
        params.cloudDensity = lighting->cloudDensity;
        params.fogDensity = lighting->fogDensity;
        params.horizonGlow = lighting->horizonGlow;
    }
    params.weatherIntensity = weatherIntensity;
    params.skyboxModelId = 0;
    params.skyboxHasStars = useOriginalSkybox;
    params.useOriginalSkybox = useOriginalSkybox;
    params.originalSkyboxAllowsAtmosphere = (skyboxFlags & 2u) != 0;
    return params;
}

}  // namespace wowee::rendering
