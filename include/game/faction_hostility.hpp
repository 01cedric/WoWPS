#pragma once

#include <cstdint>

namespace wowee {
namespace pipeline { class AssetManager; }
namespace game {

class GameHandler;

// Builds the per-FactionTemplate hostility map from FactionTemplate.dbc/
// Faction.dbc (reaction-group bitmasks, explicit enemy faction IDs, and
// Faction.dbc base reputation - the same signals CMaNGOS uses server-side)
// and installs it via GameHandler::setFactionHostileMap().
//
// GameHandler::isHostileFaction() defaults an unrecognized faction template
// ID to hostile=true (a reasonable default once the map is actually
// populated, since anything left out by the DBC-driven loop below is
// genuinely unusual). Call this once per session, as soon as playerRace is
// known and the asset manager is initialized, or every faction template
// will fall through to that hostile default.
void buildFactionHostilityMap(pipeline::AssetManager& assetManager, GameHandler& gameHandler, uint8_t playerRace);

} // namespace game
} // namespace wowee
