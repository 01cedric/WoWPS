#pragma once
#include "rendering/lighting_manager.hpp"
#include <algorithm>
#include <cmath>
namespace wowee::rendering {
inline LightingParams blendLighting(const LightingParams& a, const LightingParams& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    LightingParams r;
#define MIX(field) r.field = glm::mix(a.field, b.field, t)
    MIX(ambientColor); MIX(diffuseColor); MIX(fogColor);
    MIX(skyTopColor); MIX(skyMiddleColor); MIX(skyBand1Color); MIX(skyBand2Color);
    MIX(cloudColor); MIX(sunColor);
    MIX(fogStart); MIX(fogEnd); MIX(fogDensity); MIX(cloudDensity); MIX(horizonGlow);
    MIX(directionalDir);
#undef MIX
    const float n = glm::dot(r.directionalDir,r.directionalDir);
    r.directionalDir = n > 1.e-8f ? r.directionalDir / std::sqrt(n) : b.directionalDir;
    return r;
}
// Samples with missing profiles do not dilute valid profiles or add fallback colour.
struct LightBlendAccumulator {
    LightingParams value;
    float weight = 0;
    void add(const LightingParams& sample,float w) {
        if (!(w>0) || !std::isfinite(w)) return;
        value = weight == 0 ? sample : blendLighting(value,sample,w/(weight+w));
        weight += w;
    }
    LightingParams result(const LightingParams& fallback) const { return weight>0?value:fallback; }
};
inline float lightingBlendFactor(float dt) {
    return 1.f - std::exp(-5.f * std::clamp(std::isfinite(dt)?dt:0.f,0.f,1.f));
}
}
