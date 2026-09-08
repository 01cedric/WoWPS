// poi_marker_layer.hpp - Town/dungeon/capital POI icons on the world map.
#pragma once
#include "rendering/world_map/overlay_renderer.hpp"
#include "rendering/world_map/world_map_types.hpp"
#include <vector>

namespace wowee {
namespace rendering {
namespace world_map {

class POIMarkerLayer : public IOverlayLayer {
public:
    void setMarkers(const std::vector<POI>& markers) { markers_ = &markers; }
    void render(const LayerContext& ctx) override;
private:
    const std::vector<POI>* markers_ = nullptr;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
