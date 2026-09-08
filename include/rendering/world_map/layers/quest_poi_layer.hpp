// quest_poi_layer.hpp - Quest objective markers on the world map.
#pragma once
#include "rendering/world_map/overlay_renderer.hpp"
#include "rendering/world_map/world_map_types.hpp"
#include <vector>

namespace wowee {
namespace rendering {
namespace world_map {

class QuestPOILayer : public IOverlayLayer {
public:
    void setPois(const std::vector<QuestPOI>& pois) { pois_ = &pois; }
    void render(const LayerContext& ctx) override;
private:
    const std::vector<QuestPOI>* pois_ = nullptr;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
