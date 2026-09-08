// overlay_renderer.hpp - ImGui overlay layer system for the world map.
// Extracted from WorldMap::renderImGuiOverlay (Phase 8 of refactoring plan).
// OCP - new marker types are added by implementing IOverlayLayer.
#pragma once

#include "rendering/world_map/world_map_types.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include <optional>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <functional>
#include <imgui.h>

struct ImDrawList;

namespace wowee {
namespace rendering {
namespace world_map {

/// Context passed to each overlay layer during rendering.
struct LayerContext {
    ImDrawList* drawList = nullptr;
    ImVec2 imgMin;           // top-left of map image in screen space
    float displayW = 0, displayH = 0;
    glm::vec3 playerRenderPos;
    // The zone the server says the player is in; 0 when it has not said. Layers
    // that need "which zone is the player in" must use this in preference to
    // deriving it, so the marker agrees with the zone the map opened on.
    uint32_t playerZoneId = 0;
    float playerYawDeg = 0;
    int currentZoneIdx = -1;
    int continentIdx = -1;
    int currentMapId = -1;
    ViewLevel viewLevel = ViewLevel::ZONE;
    const std::vector<Zone>* zones = nullptr;
    const std::unordered_set<int>* exploredZones = nullptr;
    const std::unordered_set<int>* exploredOverlays = nullptr;
    const std::unordered_map<uint32_t, std::string>* areaNameByAreaId = nullptr;
    // FBO dimensions for overlay coordinate math
    int fboW = 1024;
    int fboH = 768;

    // ZMP pixel map for continent-view hover (128x128 grid of AreaTable IDs)
    const std::array<uint32_t, 128 * 128>* zmpGrid = nullptr;
    bool hasZmpData = false;
    // Function to resolve AreaTable ID → zone index (from DataRepository)
    int (*zmpResolveZoneIdx)(const void* repo, uint32_t areaId) = nullptr;
    const void* zmpRepoPtr = nullptr;   // opaque DataRepository pointer
    // ZMP-derived zone bounding boxes (zone index → UV rect on display)
    const std::unordered_map<int, ZmpRect>* zmpZoneBounds = nullptr;
};

/// What a layer needs before it can put a world position on the map: the
/// rectangle to project against, and whether that rectangle is a continent's.
struct MapProjection {
    ZoneBounds bounds;
    bool isContinent = false;
};

/// The projection for whatever the map is showing, or nothing when it is not
/// showing somewhere a world position can be placed.
///
/// Ten marker layers opened with the same three guards and the same two lines
/// deriving the bounds - the view has to be a zone or a continent, the zone
/// index has to be known, and the zone list has to be there. Three conditions
/// restated ten times is three chances for a layer to be missing one and read
/// past the end of a vector that is not there.
std::optional<MapProjection> currentProjection(const LayerContext& ctx);

/// Interface for an overlay layer rendered on top of the composite map.
class IOverlayLayer {
public:
    virtual ~IOverlayLayer() = default;
    virtual void render(const LayerContext& ctx) = 0;
};

/// Orchestrates rendering of all registered overlay layers.
class OverlayRenderer {
public:
    void addLayer(std::unique_ptr<IOverlayLayer> layer);
    void render(const LayerContext& ctx);

private:
    std::vector<std::unique_ptr<IOverlayLayer>> layers_;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
