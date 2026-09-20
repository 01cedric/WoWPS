#include "rendering/lighting_manager.hpp"
#include "rendering/zone_ambience.hpp"
#include "rendering/sun_direction.hpp"
#include "rendering/celestial_lighting.hpp"
#include <algorithm>
#include <cmath>

namespace wowee::rendering {
namespace {
float smooth(float low, float high, float x) {
    const float t = std::clamp((x-low)/(high-low), 0.f, 1.f);
    return t*t*(3.f-2.f*t);
}
float hourOfDay(float hours) {
    if (!std::isfinite(hours)) return 12.f;
    return hours - std::floor(hours / 24.f) * 24.f;
}
float luma(const glm::vec3& c) { return glm::dot(c, glm::vec3(.2126f,.7152f,.0722f)); }
// A scalar ceiling retains the authored color ratios; per-channel clipping
// would replace, for example, a violet zone with the same blue everywhere.
glm::vec3 capped(glm::vec3 color, float ceiling) {
    return color * std::min(1.f, ceiling / std::max(.00001f, luma(color)));
}
void darkenSky(LightingParams& p, float top, float horizon, glm::vec3 tint) {
    p.skyTopColor = capped(p.skyTopColor*tint,top);
    p.skyMiddleColor = capped(p.skyMiddleColor*tint,horizon);
    p.skyBand1Color = capped(p.skyBand1Color*tint,horizon*1.1f);
    p.skyBand2Color = capped(p.skyBand2Color*tint,horizon*1.2f);
    p.fogColor = capped(p.fogColor*tint,horizon);
    p.cloudColor = capped(p.cloudColor*tint,horizon*1.2f);
}
}

float outdoorNightWeight(float worldHours) {
    return smooth(-.16f,.16f,sunTravelDirection(hourOfDay(worldHours)/24.f).z);
}

float resolveZoneVisualTimeHours(uint32_t, bool, float worldTimeHours) {
    // Atmosphere never changes the clock, even in permanently gloomy zones.
    return worldTimeHours;
}

void applyOutdoorLightingPolicy(uint32_t zoneId, float worldHours, bool indoors,
                                bool underwater, bool raining, bool authoredVolumes,
                                LightingParams& p) {
    const bool coolKey = zoneId == 85 || zoneId == 130 || zoneId == 10;
    const glm::vec3 solarRay = sunTravelDirection(hourOfDay(worldHours)/24.f);
    if (indoors || underwater) {
        p.diffuseColor = celestialKeyColor(p.diffuseColor,solarRay,coolKey);
        p.sunColor = celestialKeyColor(p.sunColor,solarRay,coolKey);
        return;
    }
    const float hour = hourOfDay(worldHours);
    const float night = outdoorNightWeight(hour);
    const bool gloomy = zoneId == 85 || zoneId == 130 || zoneId == 10;
    if (gloomy) {
        const bool tirisfal = zoneId == 85;
        const bool duskwood = zoneId == 10;
        // Art direction, not physical moon illuminance. Keep original green /
        // violet fog and time-band variations while making the key cool and
        // muted. No ambient floor: authored black stays black.
        const glm::vec3 tint = tirisfal ? glm::vec3(.72f,.93f,1.f)
                                      : glm::vec3(.80f,.93f,1.f);
        p.ambientColor = capped(p.ambientColor*tint,duskwood?.19f:.25f);
        p.diffuseColor = capped(p.diffuseColor*tint,duskwood?.22f:.29f);
        p.sunColor = capped(p.sunColor*tint,.34f);
        darkenSky(p,duskwood?.055f:.16f,duskwood?.10f:.22f,tint);
        p.horizonGlow = std::min(p.horizonGlow,.08f);
        p.cloudDensity = std::max(p.cloudDensity,.80f);
        // Only the no-data fallback supplies distances. Authored local fog
        // volumes, including subzones, must retain their spatial variation.
        if (!authoredVolumes) {
            p.fogStart = std::min(p.fogStart,duskwood?35.f:60.f);
            p.fogEnd = std::min(p.fogEnd,duskwood?525.f:460.f);
            p.fogDensity = 1.f/std::max(1.f,p.fogEnd);
        }
    }
    if (night > 0.f) {
        const glm::vec3 tint(.80f,.92f,1.f);
        p.ambientColor = glm::mix(p.ambientColor,capped(p.ambientColor*tint,.22f),night);
        p.diffuseColor = glm::mix(p.diffuseColor,capped(p.diffuseColor*tint,.25f),night);
        p.sunColor = glm::mix(p.sunColor,capped(p.sunColor*tint,.32f),night);
        LightingParams moon = p;
        darkenSky(moon,.13f,.20f,tint);
        p.skyTopColor = glm::mix(p.skyTopColor,moon.skyTopColor,night);
        p.skyMiddleColor = glm::mix(p.skyMiddleColor,moon.skyMiddleColor,night);
        p.skyBand1Color = glm::mix(p.skyBand1Color,moon.skyBand1Color,night);
        p.skyBand2Color = glm::mix(p.skyBand2Color,moon.skyBand2Color,night);
        p.fogColor = glm::mix(p.fogColor,moon.fogColor,night);
        p.cloudColor = glm::mix(p.cloudColor,moon.cloudColor,night);
        p.horizonGlow *= 1.f-.85f*night;
    }
    // Warm low-angle evening light belongs to the dry red zones, not every
    // biome. Exact daylight/noon and night retain their own palette.
    if (zoneId == 14 || zoneId == 1637) {
        const float dusk = smooth(15.5f,17.25f,hour) * (1.f-smooth(18.f,19.5f,hour)) * (1.f-night);
        const float clear = 1.f-.65f*std::clamp(p.cloudDensity,0.f,1.f);
        const float warmth = dusk*clear;
        p.diffuseColor *= glm::mix(glm::vec3(1.f),glm::vec3(1.06f,.84f,.65f),warmth);
        p.sunColor *= glm::mix(glm::vec3(1.f),glm::vec3(1.f,.78f,.55f),warmth);
        p.ambientColor *= glm::mix(glm::vec3(1.f),glm::vec3(.85f,.92f,1.f),warmth);
    }
    // Original profiles already encode overcast illumination. Only the
    // missing-data fallback supplies a modest direct-light attenuation here.
    if (raining && !authoredVolumes) {
        p.diffuseColor *= .75f;
        p.sunColor *= .75f;
        p.cloudDensity = std::max(p.cloudDensity,.8f);
    }
    p.diffuseColor = celestialKeyColor(p.diffuseColor,solarRay,coolKey);
    p.sunColor = celestialKeyColor(p.sunColor,solarRay,coolKey);
}

void applyZoneAmbienceOverride(uint32_t zoneId, LightingParams& params) {
    // Kept for callers of the old fallback API; manager uses the full policy.
    applyOutdoorLightingPolicy(zoneId,12.f,false,false,false,false,params);
}
} // namespace wowee::rendering
