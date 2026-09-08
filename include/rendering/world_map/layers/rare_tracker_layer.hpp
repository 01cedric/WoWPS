// rare_tracker_layer.hpp - Nearby rare / rare-elite creature markers on the world map.
// Shows a distinctive diamond for every spawned rare currently loaded near the player,
// so a passing rare that is "out" is visible at a glance on the zone map.
#pragma once
#include "rendering/world_map/overlay_renderer.hpp"
#include "rendering/world_map/world_map_types.hpp"
#include <vector>

namespace wowee {
namespace rendering {
namespace world_map {

class RareTrackerLayer : public IOverlayLayer {
public:
    void setRares(const std::vector<RareMark>& rares) { rares_ = &rares; }
    void render(const LayerContext& ctx) override;
private:
    const std::vector<RareMark>* rares_ = nullptr;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
