#pragma once
#include <cstdint>
namespace wowee::rendering {
struct LightingParams;
// Continuous, periodic color weight. This does not move either celestial body.
float outdoorNightWeight(float worldHours);
// Called once on sampled/blended original light data, before temporal smoothing.
// Interior/underwater profiles retain their own authored lighting.
void applyOutdoorLightingPolicy(uint32_t zoneId, float worldHours, bool indoors,
                                bool underwater, bool raining, bool authoredVolumes,
                                LightingParams& params);
}
