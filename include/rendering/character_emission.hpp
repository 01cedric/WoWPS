#pragma once
#include <glm/glm.hpp>

namespace wowee::rendering {
// The named candle profile previously used albedo * (1 + tint * boost).
// Encode its existing gain explicitly for the multiplicative emission shader;
// generic unlit batches keep neutral white tint and unit boost.
inline glm::vec3 preservedCandleEmissionTint(const glm::vec3& tint, float boost) {
    return glm::vec3(1.0f) + tint * boost;
}
} // namespace wowee::rendering
