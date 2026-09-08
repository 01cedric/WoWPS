// chest_tracker_layer.hpp - Nearby spawned chest markers on the world map.
#pragma once
#include "rendering/world_map/overlay_renderer.hpp"
#include "rendering/world_map/world_map_types.hpp"
#include <vector>

namespace wowee {
namespace rendering {
namespace world_map {

class ChestTrackerLayer : public IOverlayLayer {
public:
    void setChests(const std::vector<ChestMark>& chests) { chests_ = &chests; }
    void render(const LayerContext& ctx) override;

private:
    const std::vector<ChestMark>* chests_ = nullptr;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
