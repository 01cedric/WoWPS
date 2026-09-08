// map_resolver.hpp - Centralized map navigation resolution for the world map.
// Determines the correct action when clicking a region or zone at any view level.
// All functions are stateless free functions - trivially testable.
// Map folder names are resolved from a built-in table matching
// Data/interface/worldmap/ rather than WorldLoader::mapIdToName.
#pragma once

#include "rendering/world_map/world_map_types.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace rendering {
namespace world_map {

// ── Map folder lookup (replaces WorldLoader::mapIdToName for world map) ──

/// Map ID → worldmap folder name (e.g. 0 → "Azeroth", 571 → "Northrend").
/// Returns empty string if unknown.
const char* mapIdToFolder(uint32_t mapId);

/// Worldmap folder name → map ID (e.g. "Azeroth" → 0, "Northrend" → 571).
/// Case-insensitive comparison. Returns -1 if unknown.
int folderToMapId(const std::string& folder);

/// Map ID → display name for UI (e.g. 0 → "Eastern Kingdoms", 571 → "Northrend").
/// Returns nullptr if unknown.
const char* mapDisplayName(uint32_t mapId);

// ── Result types ─────────────────────────────────────────────

enum class MapResolveAction {
    NONE,                ///< No valid navigation target
    NAVIGATE_CONTINENT,  ///< Switch to continent view within current map data
    LOAD_MAP,            ///< Load a different map entirely (switchToMap)
    ENTER_ZONE,          ///< Enter zone view within current continent
};

struct MapResolveResult {
    MapResolveAction action = MapResolveAction::NONE;
    int targetZoneIdx = -1;      ///< Zone index for NAVIGATE_CONTINENT or ENTER_ZONE
    std::string targetMapName;   ///< Map folder name for LOAD_MAP
};

// ── Resolve functions ────────────────────────────────────────

/// Resolve WORLD view region click. Determines whether to navigate within
/// the current map data (e.g. clicking EK when already on Azeroth) or load
/// a new map (e.g. clicking Kalimdor or Northrend from Azeroth world view).
MapResolveResult resolveWorldRegionClick(uint32_t regionMapId,
                                          const std::vector<Zone>& zones,
                                          int currentMapId,
                                          int cosmicIdx);

/// Resolve CONTINENT view zone click. Determines whether the clicked zone
/// can be entered directly (same map) or requires loading a different map
/// (zone's displayMapID differs from current).
MapResolveResult resolveZoneClick(int zoneIdx,
                                   const std::vector<Zone>& zones,
                                   int currentMapId);

/// Resolve COSMIC view map click. Always returns LOAD_MAP for the target.
MapResolveResult resolveCosmicClick(uint32_t targetMapId);

/// Find the best continent zone index to display for a given mapId within
/// the currently loaded zones. Prefers leaf continents over root continents.
/// Returns -1 if no suitable continent is found.
int findContinentForMapId(const std::vector<Zone>& zones,
                           uint32_t mapId,
                           int cosmicIdx);

} // namespace world_map
} // namespace rendering
} // namespace wowee
