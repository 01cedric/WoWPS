#pragma once

// Pure layout arithmetic for the buff bar, kept out of the ImGui code so it can be
// tested. The bar is a single row along the top of the screen, right of centre: it is
// right-aligned to end just left of the minimap and grows leftwards as auras are
// gained. Nothing wraps to a second row, so the row has a hard capacity and the
// arithmetic that decides it is worth pinning down.

#include <algorithm>
#include <cstdint>

namespace wowee {
namespace ui {

struct BuffBarLayout {
    float iconSize = 0.0f;     // per-icon edge length, in pixels
    float iconSpacing = 0.0f;  // gap between icons
    float barWidth = 0.0f;     // window width
    float barX = 0.0f;         // window left edge, in screen pixels
    float barY = 0.0f;         // window top edge
    int maxIcons = 0;          // how many icons fit on the row at all
    int auraShown = 0;         // auras that made the cut
    int enchantShown = 0;      // weapon enchants that made the cut
    int iconCount = 0;         // auraShown + enchantShown
};

struct BuffBarMetrics {
    // 25% larger than the original 32px icons, at the 1080p reference height.
    static constexpr float kBaseIconSize = 40.0f;
    static constexpr float kBaseIconSpacing = 2.0f;
    static constexpr float kWindowPadding = 16.0f;  // 8px each side
    static constexpr float kReferenceHeight = 1080.0f;
    // The minimap is 200x200 at a 10px margin, so its left edge sits 210px in from the
    // right of the screen. The row must stop before it.
    static constexpr float kMinimapLeftEdge = 210.0f;
    static constexpr float kRightGap = 10.0f;
    static constexpr float kScreenMargin = 10.0f;
    static constexpr float kTopMargin = 10.0f;
    // Floor: the Dismiss Pet button needs somewhere to sit even with no auras.
    static constexpr float kMinBarWidth = 140.0f;
    // Bounds on the automatic resolution scaling.
    static constexpr float kMinAutoScale = 0.75f;
    static constexpr float kMaxAutoScale = 2.0f;
};

/// How much of the right edge of the screen the minimap claims, in pixels.
///
/// This client's own minimap is 200px at a 10px margin whatever the resolution, so
/// a constant was enough. FrameXML's MinimapCluster is 192 interface units wide,
/// and an interface unit is a fixed fraction of the window height - at 1528px tall
/// the cluster is 382px, nearly twice the constant. A row right-aligned against the
/// smaller number therefore ends up underneath the minimap rather than beside it,
/// which is what the buff bar did as soon as FrameXML took the minimap over.
inline float minimapReservedWidth(float screenH, bool frameXmlMinimap) {
    if (!frameXmlMinimap) return BuffBarMetrics::kMinimapLeftEdge;
    // MinimapCluster's width, converted out of interface units. FrameXML is
    // authored against a screen 768 units tall whatever the real height is.
    constexpr float kClusterUnits = 192.0f;
    constexpr float kInterfaceHeight = 768.0f;
    return kClusterUnits * (screenH / kInterfaceHeight);
}

/// Icons track the window height so the bar keeps its proportions at any resolution,
/// with the user's Buff Bar Scale setting layered on top.
inline float buffBarScale(float screenH, float userScale) {
    const float autoScale = std::clamp(screenH / BuffBarMetrics::kReferenceHeight,
                                       BuffBarMetrics::kMinAutoScale,
                                       BuffBarMetrics::kMaxAutoScale);
    return autoScale * userScale;
}

/// Lay out a single row of `auraCount` auras plus `enchantCount` weapon enchants.
///
/// Weapon enchants claim their slots first: a wall of buffs must not push a sharpening
/// stone off the row. Anything that still does not fit is dropped rather than wrapped.
inline BuffBarLayout computeBuffBarLayout(float screenW, float screenH, float userScale,
                                          int auraCount, int enchantCount,
                                          float minimapWidth =
                                              BuffBarMetrics::kMinimapLeftEdge) {
    using M = BuffBarMetrics;

    BuffBarLayout out;
    const float scale = buffBarScale(screenH, userScale);
    out.iconSize = M::kBaseIconSize * scale;
    out.iconSpacing = M::kBaseIconSpacing * scale;
    out.barY = M::kTopMargin;

    // Space between the left edge of the screen and the left edge of the minimap.
    const float availableW = screenW - minimapWidth - M::kRightGap - M::kScreenMargin;
    out.maxIcons = std::max(1, static_cast<int>(
        (availableW - M::kWindowPadding + out.iconSpacing) / (out.iconSize + out.iconSpacing)));

    out.enchantShown = std::clamp(enchantCount, 0, out.maxIcons);
    out.auraShown = std::clamp(auraCount, 0, out.maxIcons - out.enchantShown);
    out.iconCount = out.auraShown + out.enchantShown;

    out.barWidth = M::kWindowPadding;
    if (out.iconCount > 0) {
        out.barWidth += out.iconCount * out.iconSize +
                        (out.iconCount - 1) * out.iconSpacing;
    }
    out.barWidth = std::max(out.barWidth, M::kMinBarWidth * scale);

    // Right-align against the minimap's left edge, but never run off screen.
    out.barX = std::max(M::kScreenMargin,
                        screenW - minimapWidth - M::kRightGap - out.barWidth);
    return out;
}

} // namespace ui
} // namespace wowee
