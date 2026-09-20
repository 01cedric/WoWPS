#include "rendering/celestial_lighting.hpp"
#include "rendering/sky_params_from_lighting.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace wowee::rendering;

int main() {
    for (int tick = 0; tick < 86400; ++tick) {
        const float hour = float(tick) / 3600.0f;
        const auto solar = sunTravelDirection(hour / 24.0f);
        const auto key = outdoorKeyLightTravelDirection(solar);
        const float sun = celestialDiscStrength(solar, false);
        const float moon = celestialDiscStrength(solar, true);
        const float strength = celestialKeyStrength(solar);
        assert(sun >= 0 && sun <= 1 && moon >= 0 && moon <= 1);
        assert(sun * moon == 0); // Never two lit celestial keys.
        assert(std::abs(sun + moon - strength) < 1.e-6f);
        if (strength > 0.0f)
            assert(glm::dot(-key, moon > 0 ? solar : -solar) > 0.99999f);
        assert(std::abs(celestialKeyStrength(solar * 5.0f) - strength) < 1.e-5f);
    }
    for (float horizon : {-1.e-6f, 0.0f, 1.e-6f}) {
        assert(celestialKeyStrength({1, 0, horizon}) < 1.e-8f);
        assert(celestialDiscStrength({1, 0, horizon}, false) < 1.e-8f);
        assert(celestialDiscStrength({1, 0, horizon}, true) < 1.e-8f);
    }
    assert(celestialKeyStrength({0, 0, 0}) == 0);
    assert(celestialKeyStrength(glm::vec3(std::numeric_limits<float>::quiet_NaN())) == 0);
    assert(glm::length(celestialSolarTravelDirection({}, 12) - sunTravelDirection(0.5f)) < 1.e-6f);
    assert(celestialDiscStrength(sunTravelDirection(0.0f), false) == 0);
    assert(celestialDiscStrength(sunTravelDirection(0.5f), true) == 0);

    // Color bridge used by normal, threaded and reflection rendering paths.
    // In particular a cool Tirisfal input must not acquire the old warm tint.
    LightingParams lighting;
    lighting.sunColor = {0.12f, 0.20f, 0.28f};
    lighting.directionalDir = sunTravelDirection(0.1f);
    lighting.skyTopColor = {0.08f, 0.11f, 0.16f};
    lighting.cloudColor = {0.15f, 0.18f, 0.24f};
    lighting.diffuseColor = {0.08f, 0.12f, 0.20f};
    lighting.ambientColor = {0.03f, 0.04f, 0.08f};
    const auto params = skyParamsFromLighting(2.4f, 2.4f, 0.7f, &lighting, true, 2u);
    assert(params.sunColor == lighting.sunColor);
    assert(celestialDiscColor(params.sunColor) == lighting.sunColor);
    assert(params.skyTopColor == lighting.skyTopColor && params.cloudColor == lighting.cloudColor);
    assert(params.directionalDir == lighting.directionalDir);
    assert(params.directionalColor == lighting.diffuseColor && params.ambientColor == lighting.ambientColor);
    assert(params.timeOfDay == 2.4f && params.gameTime == 2.4f);
    assert(params.useOriginalSkybox && params.originalSkyboxAllowsAtmosphere && params.skyboxHasStars);
    assert(!skyParamsFromLighting(12, 12, 0, &lighting, true, 0).originalSkyboxAllowsAtmosphere);
    assert(animatedMoonPhase(0.5f, 0, 240) == 0.5f);
    assert(animatedMoonPhase(0.25f, 0, 210) == 0.25f);
    assert(animatedMoonPhase(0.5f, 120, 240) == 0.0f);
    assert(animatedMoonPhase(0.5f, 240, 240) == 0.5f);
    puts("PASS 86400 solar seconds: disc/key alignment, horizon fade, invalid input, cool authored color, real clock, M2 atmosphere flags and optional phase animation");
}
