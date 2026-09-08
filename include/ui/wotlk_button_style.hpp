#pragma once

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace wowee::ui::glue_button {

// DialogButton textures in the 3.3.5 GlueBasicControls.xml use the complete
// 80 x 22 artwork in a 128 x 32 image (U: 0..0.625, V: 0..0.6875).
// Splitting at 64/128 loses the right-hand bevel and produces an open edge.
constexpr float kRight = 80.0f / 128.0f;
constexpr float kBottom = 22.0f / 32.0f;
constexpr float kCapU = 8.0f / 128.0f;

inline void drawStrip(ImDrawList* list, ImTextureID texture, ImVec2 a, ImVec2 b,
                      ImU32 tint = IM_COL32_WHITE) {
    if (!list || b.x <= a.x || b.y <= a.y) return;
    const float cap = std::min((b.y - a.y) * (8.0f / 22.0f), (b.x - a.x) * .5f);
    list->AddImage(texture, a, {a.x + cap, b.y}, {0, 0}, {kCapU, kBottom}, tint);
    if (b.x - a.x > cap * 2.0f)
        list->AddImage(texture, {a.x + cap, a.y}, {b.x - cap, b.y},
                       {kCapU, 0}, {kRight - kCapU, kBottom}, tint);
    list->AddImage(texture, {b.x - cap, a.y}, b,
                   {kRight - kCapU, 0}, {kRight, kBottom}, tint);
}

// Change the red enamel to blue once, before upload. Multiplying a blue tint
// into red texels would make the fill black. Leave the original neutral/gold
// bevel, transparent edge pixels and highlight untouched, and keep the source
// shading/saturation and distinct darker pressed artwork.
inline size_t blueEnamel(std::span<uint8_t> rgba) {
    size_t changed = 0;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (!rgba[i + 3]) continue;
        const float r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
        const float low = std::min(g, b);
        const float chroma = r - low;
        if (chroma < 8.0f || r <= g || r <= b) continue;
        // Red's hue lies around 0 degrees; the gold bevel lies beyond 30.
        const float hueFraction = (g - b) / chroma;
        if (hueFraction >= .5f || hueFraction <= -.5f) continue;
        const float blend = std::clamp((.5f - std::abs(hueFraction)) * 6.0f, 0.0f, 1.0f);
        const auto mix = [blend](float original, float target) {
            return static_cast<uint8_t>(std::clamp(original + (target - original) * blend,
                                                    0.0f, 255.0f) + .5f);
        };
        rgba[i] = mix(r, low);
        rgba[i + 1] = mix(g, low + chroma * .5f);
        rgba[i + 2] = mix(b, r);
        ++changed;
    }
    return changed;
}

} // namespace wowee::ui::glue_button
