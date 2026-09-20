#pragma once
#include <cstdint>

namespace wowee::rendering {
// The near region retains the quality setting's N x N resolution. Far terrain
// and shafts retain their complete footprint at N/2 x N/2 raster cost.
struct ShadowAtlasRegion {
    uint32_t x, y, side;
};
constexpr ShadowAtlasRegion shadowAtlasRegion(uint32_t side, uint32_t cascade) {
    return cascade == 0 ? ShadowAtlasRegion{0, 0, side}
                        : ShadowAtlasRegion{side, 0, side / 2};
}
}
