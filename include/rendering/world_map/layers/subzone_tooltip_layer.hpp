// subzone_tooltip_layer.hpp - Overlay area hover labels in zone view.
#pragma once
#include "rendering/world_map/overlay_renderer.hpp"

namespace wowee {
namespace rendering {
namespace world_map {

class SubzoneTooltipLayer : public IOverlayLayer {
public:
    void render(const LayerContext& ctx) override;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
