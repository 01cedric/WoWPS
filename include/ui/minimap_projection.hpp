#pragma once

#include <glm/glm.hpp>

namespace wowee::ui {

/// The minimap draws the world rotated around the player and scaled from yards
/// to pixels. Both directions of that transform live here because the client
/// needs both: markers project outward, clicks invert back.
///
/// Offsets are minimap pixels measured from its centre with **+y down**, which
/// is ImGui's screen space and the space the marker code already worked in.
/// Deltas are render space, relative to the player.
struct MinimapView {
    /// Yards the minimap covers, centre to edge.
    float viewRadius = 100.0f;
    /// Half the minimap's on-screen width.
    float mapRadius = 70.0f;
    /// Clockwise camera bearing from north, pre-resolved. Both are 1/0 when the
    /// map does not rotate with the camera, which is this client's default.
    float cosBearing = 1.0f;
    float sinBearing = 0.0f;
};

/// Render-space delta from the player to an offset in minimap pixels.
///
/// Exact inverse of the minimap display shader:
///   mapUV = playerUV + vec2(rotated.y, -rotated.x) * zoom * 2
/// where rotated = R(bearing) * vec2(-center.x, center.y). Render +X is west
/// and +Y is north, while composite UV grows east/south - hence the negations.
inline glm::vec2 renderDeltaToMinimapOffset(float dx, float dy, const MinimapView& v) {
    const float rx = -(dy * v.cosBearing - dx * v.sinBearing);
    const float ry = -(dy * v.sinBearing + dx * v.cosBearing);
    if (v.viewRadius <= 0.0f) return {0.0f, 0.0f};
    return {rx / v.viewRadius * v.mapRadius, ry / v.viewRadius * v.mapRadius};
}

/// An offset in minimap pixels back to a render-space delta from the player.
inline glm::vec2 minimapOffsetToRenderDelta(float px, float py, const MinimapView& v) {
    if (v.mapRadius <= 0.0f) return {0.0f, 0.0f};
    const float rx = px / v.mapRadius * v.viewRadius;
    const float ry = py / v.mapRadius * v.viewRadius;
    const float oldRx = -rx;
    const float rotX = oldRx * v.cosBearing - ry * v.sinBearing;
    const float rotY = oldRx * v.sinBearing + ry * v.cosBearing;
    return {-rotY, rotX};
}

}  // namespace wowee::ui
