// party_dot_layer.hpp - Party member position dots on the world map.
#pragma once
#include "rendering/world_map/overlay_renderer.hpp"
#include "rendering/world_map/world_map_types.hpp"
#include <vector>

namespace wowee {
namespace rendering {
namespace world_map {

class PartyDotLayer : public IOverlayLayer {
public:
    void setDots(const std::vector<PartyDot>& dots) { dots_ = &dots; }
    void render(const LayerContext& ctx) override;
private:
    const std::vector<PartyDot>* dots_ = nullptr;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
