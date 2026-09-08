#pragma once

#include "pipeline/adt_loader.hpp"
#include <string>
#include <vector>

namespace wowee {
namespace pipeline {

// Loader for the Wowee Open Terrain format (.wot/.whm)
// Novel format - no Blizzard structures, fully portable
class WoweeTerrainLoader {
public:
    // Load terrain from .whm binary heightmap file
    static bool loadHeightmap(const std::string& whmPath, ADTTerrain& terrain);

    // Load terrain metadata from .wot JSON file
    static bool loadMetadata(const std::string& wotPath, ADTTerrain& terrain);

    // Full load: .wot metadata + .whm heightmap
    static bool load(const std::string& basePath, ADTTerrain& terrain);

    // Check if a wowee open terrain exists at the given base path
    static bool exists(const std::string& basePath);
};

} // namespace pipeline
} // namespace wowee
