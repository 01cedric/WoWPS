#pragma once

#include <vector>

namespace wowee {
namespace ui {

/// One nameplate's screen box: its bar plus the name line above it.
struct PlateBox {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

/// Where a plate's bar goes so it clears every plate already placed.
///
/// Allow Nameplate Overlap, turned off, means two units at the same distance
/// must not print one bar through another. A plate that would land on one
/// already placed is lifted above it and asked again, because the place it
/// moved to may be taken as well.
///
/// Returns the bar's top edge. Callers place nearest first, so the plate that
/// moves is the one further away.
///
/// The guard is not decoration. Each lift moves strictly upward, so the loop
/// ends on any sane input - but "sane" here means boxes produced by a camera
/// projection, and a degenerate frame that put forty plates on one pixel would
/// otherwise walk the whole list forty times before drawing anything.
inline float plateTopClearOf(const std::vector<PlateBox>& placed,
                             float x0, float x1, float y,
                             float barHeight, float topExtent,
                             int maxLifts = 24) {
    for (int guard = 0; guard < maxLifts; ++guard) {
        bool lifted = false;
        for (const auto& r : placed) {
            const bool xHit = (x0 - 2.0f) < r.x1 && (x1 + 2.0f) > r.x0;
            const bool yHit = (y - topExtent) < r.y1 && (y + barHeight) > r.y0;
            if (xHit && yHit) {
                y = r.y0 - barHeight - 2.0f;
                lifted = true;
                break;
            }
        }
        if (!lifted) break;
    }
    return y;
}

} // namespace ui
} // namespace wowee
