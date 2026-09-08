#pragma once

#include <algorithm>
#include <cmath>

namespace wowee::rendering {

// The legacy client stores even its wide loading paintings in square BLPs.
// Their logical aspect comes from LoadingScreens.dbc, never the storage size.
struct LoadingRect { float x = 0, y = 0, width = 0, height = 0; };
struct LoadingScreenLayout {
    LoadingRect painting;
    LoadingRect border;
    LoadingRect fill;
    float fillU = 0;
};

inline float advanceLoadingProgress(float previous, float requested) {
    if (!std::isfinite(previous)) previous = 0;
    previous = std::clamp(previous, 0.0f, 1.0f);
    if (!std::isfinite(requested)) return previous;
    return std::max(previous, std::clamp(requested, 0.0f, 1.0f));
}

inline LoadingScreenLayout layoutLoadingScreen(float width, float height,
                                                bool wide, float progress) {
    LoadingScreenLayout result;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return result;
    const float aspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f;
    const float paintHeight = std::min(height, width / aspect);
    const float paintWidth = paintHeight * aspect;
    result.painting = {(width - paintWidth) * 0.5f, (height - paintHeight) * 0.5f,
                       paintWidth, paintHeight};
    // Legacy bottom-centred loading bar, on the client's 768-high UI canvas.
    // Border is painted over the fill so its original ornamental ends and
    // transparent aperture are retained. No stage label or percentage belongs
    // on the original loading painting.
    const float scale = std::min(height / 768.0f, width / 1024.0f);
    result.border = {(width - 512.0f * scale) * 0.5f, height - 64.0f * scale,
                     512.0f * scale, 64.0f * scale};
    result.fillU = advanceLoadingProgress(0, progress);
    result.fill = {result.border.x + 18.0f * scale, result.border.y + 23.0f * scale,
                   476.0f * scale * result.fillU, 18.0f * scale};
    return result;
}

} // namespace wowee::rendering
