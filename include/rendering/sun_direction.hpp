#pragma once

/**
 * sun_direction.hpp - which way the sunlight travels at a given hour.
 *
 * Light.dbc and its band tables give a zone its colours and say nothing about
 * where the sun is: the client derives that from the world clock alone, so
 * every zone on a map is lit from the same angle at the same minute and only
 * the colours differ. This is that derivation.
 *
 * It lives here because it was written out twice in lighting_manager.cpp - once
 * for the path that blends real LightParams volumes and once for the fallback
 * used where no volume reaches - and the two copies have to agree exactly. They
 * did, but only by hand: walking out of the last volume's range switches
 * between them mid-stride, and any drift between the two would read as the sun
 * jumping across the sky at a zone edge.
 *
 * The vector points the way the light *travels*, which is away from the sun.
 * That is what every lit shader wants for N-dot-L and what
 * LightingParams::directionalDir has always held; callers that want the
 * direction to the sun - the godray pass, the lens flare - negate it.
 *
 * World space here is the client's: +z is up.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace wowee {
namespace rendering {

/// Light.dbc keys its bands in half-minutes: 24 * 60 * 2 ticks to the day.
inline constexpr uint16_t kLightHalfMinutesPerDay = 2880;

/**
 * The direction sunlight travels at a fraction of the way through the day.
 *
 * @param dayFraction  0 at midnight, 0.5 at noon. Values outside 0..1 wrap,
 *                     because the caller's clock does.
 *
 * The shape of the curve is worth stating, because the constants look
 * arbitrary. The x/z pair sweeps a circle so the sun rises on one side and
 * sets on the other; z is the only component that changes sign, so it alone
 * decides day from night, and the pass that draws sunbeams reads exactly that
 * to know whether there is a sun to scatter. The -0.6 on y is a fixed tilt
 * rather than part of the sweep: it keeps the light coming from slightly
 * behind the world's north the whole day, which is the bias the original
 * client's outdoor shading has and what stops noon from being a light straight
 * down with no shadows in it at all.
 */
[[nodiscard]] inline glm::vec3 sunTravelDirection(float dayFraction) {
    const float angle = dayFraction * glm::two_pi<float>();
    return glm::normalize(glm::vec3(std::sin(angle) * 0.6f,
                                    -0.6f + std::cos(angle) * 0.4f,
                                    std::cos(angle) * 0.6f));
}

// Keep the celestial sun path unchanged. World shading and shadow projection
// share this Z-up light-ray convention instead: nighttime key light must not
// illuminate the undersides of terrain and buildings from below the horizon.
[[nodiscard]] inline glm::vec3 outdoorKeyLightTravelDirection(glm::vec3 ray) {
    const float len2 = glm::dot(ray, ray);
    if (!std::isfinite(len2) || len2 < 1.0e-8f)
        return glm::normalize(glm::vec3(0.3f, -0.7f, -0.6f));
    ray /= std::sqrt(len2);
    // Keep outdoor key light decisively above the scene.  The client sky may
    // keep its authored horizon motion, but surface lighting should not read
    // as a side/below light on steep terrain and character faces.
    ray.x *= 0.35f;
    ray.y *= 0.35f;
    ray.z = -std::max(std::abs(ray.z), 0.85f);
    return glm::normalize(ray);
}

/// The same, keyed the way the DBC bands are - so the sun and the colours
/// sampled beside it are read off one clock rather than two.
[[nodiscard]] inline glm::vec3 sunTravelDirectionAtHalfMinutes(uint16_t timeHalfMinutes) {
    return sunTravelDirection(static_cast<float>(timeHalfMinutes) /
                              static_cast<float>(kLightHalfMinutesPerDay));
}

}  // namespace rendering
}  // namespace wowee
