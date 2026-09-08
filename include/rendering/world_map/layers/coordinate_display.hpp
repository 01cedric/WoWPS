// coordinate_display.hpp - WoW coordinates under cursor on the world map.
#pragma once
#include "rendering/world_map/overlay_renderer.hpp"

namespace wowee {
namespace rendering {
namespace world_map {

class CoordinateDisplay : public IOverlayLayer {
public:
    void render(const LayerContext& ctx) override;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
