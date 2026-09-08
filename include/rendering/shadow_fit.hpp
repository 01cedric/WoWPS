#pragma once

/**
 * shadow_fit.hpp - where the orthographic shadow camera should sit, and how
 * wide it has to be.
 *
 * The shadow projection used to be a fixed box of `shadowDistance` half-extent
 * centred on the player, which is a square 2x the shadow distance on a side -
 * 480 yards on desktop. Most of that box is behind the camera or outside the
 * field of view, and every texel spent there is a texel not spent on what the
 * player is looking at. Fitting the box to the part of the view frustum that
 * is actually within shadow range recovers that: at a 45-degree vertical field
 * of view and 240 yards of range the fitted radius is around 130 yards, so the
 * same map holds roughly twice the resolution per yard in each axis.
 *
 * The fit is a bounding *sphere* rather than a bounding box on purpose. A box
 * fitted to the frustum changes size as the camera turns, and a shadow map
 * whose world-per-texel changes every frame crawls: edges swim along
 * themselves while the player merely looks around. A sphere is invariant under
 * rotation, so turning the camera changes only where the box is, never how big
 * it is - and that residual translation is removed by snapping the centre to
 * the texel grid.
 */

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace wowee {
namespace rendering {

/// The sphere that contains the shadowed slice of the view frustum.
struct ShadowFrustumFit {
    glm::vec3 center{0.0f};  ///< World-space centre, on the camera's view axis.
    float radius = 1.0f;     ///< Its radius in yards; the ortho half-extent.
};

/**
 * Fit the shadow box to the view frustum between the near plane and
 * `shadowDistance`, whichever of the far plane and that is nearer.
 *
 * @param camPos          Camera position, world space.
 * @param camForward      Camera view axis; need not be unit length.
 * @param fovYRadians     Vertical field of view.
 * @param aspect          Width over height.
 * @param nearPlane       Camera near plane, in yards.
 * @param shadowDistance  How far shadows are wanted, in yards.
 */
[[nodiscard]] inline ShadowFrustumFit fitShadowFrustum(const glm::vec3& camPos,
                                                       const glm::vec3& camForward,
                                                       float fovYRadians, float aspect,
                                                       float nearPlane,
                                                       float shadowDistance) {
    ShadowFrustumFit fit;

    // Degenerate inputs answer with something usable rather than a NaN that
    // would reach a projection matrix. A zero-length forward has no view axis
    // to place a centre on, so the player's own position is the honest answer.
    const float fwdLenSq = glm::dot(camForward, camForward);
    if (!(fwdLenSq > 1e-12f) || !std::isfinite(shadowDistance) || shadowDistance <= 0.0f) {
        fit.center = camPos;
        fit.radius = std::max(1.0f, shadowDistance);
        return fit;
    }
    const glm::vec3 fwd = camForward * glm::inversesqrt(fwdLenSq);

    const float n = std::max(0.01f, nearPlane);
    const float f = std::max(n + 0.01f, shadowDistance);

    // k is the radial spread of a frustum corner per yard of depth: the corner
    // offset is (d*tanH, d*tanV) and its length is d*sqrt(tanH^2 + tanV^2).
    const float tanV = std::tan(std::clamp(fovYRadians, 0.01f, 3.0f) * 0.5f);
    const float tanH = tanV * std::max(0.01f, aspect);
    const float kSq = tanH * tanH + tanV * tanV;

    // Put the centre where the near and far corners are equidistant. Solving
    // (d-n)^2 + n^2 k^2 == (f-d)^2 + f^2 k^2 gives d = (f+n)(1+k^2)/2. Past a
    // wide enough field of view that lands beyond the far plane, which means
    // the far corners bound everything on their own and the far plane centre
    // is the tightest sphere available.
    float d = (f + n) * (1.0f + kSq) * 0.5f;
    if (d > f) d = f;

    fit.center = camPos + fwd * d;
    const float dz = f - d;
    fit.radius = std::sqrt(dz * dz + f * f * kSq);
    return fit;
}

/**
 * Snap a shadow centre onto the shadow map's own texel grid.
 *
 * Without this the projection slides by a fraction of a texel every frame the
 * player moves, and every shadow edge in the world crawls with it. The centre
 * is quantised along the light's two lateral axes only - moving it along the
 * light direction changes nothing about which texel a surface lands in.
 *
 * @param texelWorld  World size of one shadow map texel: 2*radius / mapSide.
 */
[[nodiscard]] inline glm::vec3 snapShadowCenter(const glm::vec3& center,
                                                const glm::vec3& lightRight,
                                                const glm::vec3& lightUp,
                                                const glm::vec3& lightDir,
                                                float texelWorld) {
    if (!(texelWorld > 1e-6f)) return center;
    const float r = std::floor(glm::dot(center, lightRight) / texelWorld) * texelWorld;
    const float u = std::floor(glm::dot(center, lightUp) / texelWorld) * texelWorld;
    const float dpt = glm::dot(center, lightDir);
    return lightRight * r + lightUp * u + lightDir * dpt;
}

}  // namespace rendering
}  // namespace wowee
