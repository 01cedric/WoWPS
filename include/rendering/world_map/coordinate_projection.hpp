// coordinate_projection.hpp - Pure coordinate math for world map UV projection.
// Extracted from WorldMap methods (Phase 2 of refactoring plan).
// All functions are stateless free functions - trivially testable.
#pragma once

#include "rendering/world_map/world_map_types.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace wowee {
namespace rendering {
namespace world_map {

/// Project render-space position to [0,1] UV on a zone or continent map.
glm::vec2 renderPosToMapUV(const glm::vec3& renderPos,
                            const ZoneBounds& bounds,
                            bool isContinent);

/// Derive effective projection bounds for a continent from its child zones.
/// Uses zoneBelongsToContinent() internally. Returns false if insufficient data.
bool getContinentProjectionBounds(const std::vector<Zone>& zones,
                                   int contIdx,
                                   float& left, float& right,
                                   float& top, float& bottom);

/// The bounds a zone is projected against, and whether it is a continent.
///
/// A continent is drawn against bounds derived from its child zones, not its
/// own: WorldMapArea gives a continent a box that does not match where its zones
/// actually are, so projecting against that box puts every marker in the wrong
/// place. A zone uses its own bounds.
///
/// Nine places worked this out for themselves - every marker layer and the
/// facade - which is nine chances for one of them to project against the wrong
/// rectangle and put its markers somewhere the others do not.
ZoneBounds projectionBoundsFor(const std::vector<Zone>& zones, int zoneIdx,
                               bool& isContinent);

/// Find the best-fit continent index for a player position.
/// Prefers the smallest containing continent; falls back to nearest center.
int findBestContinentForPlayer(const std::vector<Zone>& zones,
                                const glm::vec3& playerRenderPos);

/// Find the zone the player is in.
///
/// authoritativeZoneId is the zone the server says the player is in, from
/// SMSG_INIT_WORLD_STATES. When it names a zone on this map that is the answer,
/// full stop - no geometry involved.
///
/// The fallback below is a guess and reads like one. WorldMapArea bounds are
/// axis-aligned boxes around irregular zones, so neighbours overlap heavily and
/// the best it can do is pick the box the player sits deepest inside. That is
/// how the map came to open on a zone the player was merely near.
///
/// Pass 0 when the server has not said (before the first world-state packet, or
/// on expansions that do not send one) and the geometry is used alone.
int findZoneForPlayer(const std::vector<Zone>& zones,
                       const glm::vec3& playerRenderPos,
                       uint32_t authoritativeZoneId = 0);

/// Index of the zone with this AreaID, or -1. Exact, no geometry.
int findZoneByAreaId(const std::vector<Zone>& zones, uint32_t areaId);

/// Test if a zone spatially belongs to a given continent.
/// Uses parentWorldMapID when available, falls back to overlap heuristic.
bool zoneBelongsToContinent(const std::vector<Zone>& zones,
                             int zoneIdx, int contIdx);

/// Check whether the zone at idx is a root continent (has leaf continents as children).
bool isRootContinent(const std::vector<Zone>& zones, int idx);

/// Check whether the zone at idx is a leaf continent (parentWorldMapID != 0, areaID == 0).
bool isLeafContinent(const std::vector<Zone>& zones, int idx);

} // namespace world_map
} // namespace rendering
} // namespace wowee
