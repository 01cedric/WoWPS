#include "pipeline/wowee_minimap_levels.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'N', 'L'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wmnl";

} // namespace

const WoweeMinimapLevels::Entry*
WoweeMinimapLevels::findById(uint32_t levelId) const {
    for (const auto& e : entries)
        if (e.levelId == levelId) return &e;
    return nullptr;
}

const WoweeMinimapLevels::Entry*
WoweeMinimapLevels::findContainingZ(uint32_t mapId,
                                      uint32_t areaId,
                                      float z) const {
    for (const auto& e : entries) {
        if (e.mapId != mapId || e.areaId != areaId) continue;
        if (z >= e.minZ && z < e.maxZ) return &e;
    }
    return nullptr;
}

std::vector<const WoweeMinimapLevels::Entry*>
WoweeMinimapLevels::findByArea(uint32_t mapId,
                                  uint32_t areaId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.mapId == mapId && e.areaId == areaId)
            out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->levelIndex < b->levelIndex;
              });
    return out;
}

bool WoweeMinimapLevelsLoader::save(const WoweeMinimapLevels& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeMinimapLevels::Entry& e) {
        writePOD(os, e.levelId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.areaId);
        writePOD(os, e.levelIndex);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.minZ);
        writePOD(os, e.maxZ);
        writeStr(os, e.texturePath);
        writeStr(os, e.displayName);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeMinimapLevels WoweeMinimapLevelsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeMinimapLevels>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeMinimapLevels::Entry& e) {
        if (!readPOD(is, e.levelId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.areaId) ||
            !readPOD(is, e.levelIndex) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.minZ) ||
            !readPOD(is, e.maxZ)) { return false; }
        if (!readStr(is, e.texturePath) ||
            !readStr(is, e.displayName)) { return false; }
        if (!readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeMinimapLevelsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeMinimapLevels WoweeMinimapLevelsLoader::makeStormwind(
    const std::string& catalogName) {
    using M = WoweeMinimapLevels;
    WoweeMinimapLevels c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t levelIndex, float minZ, float maxZ,
                    const char* texPath, const char* displayName,
                    const char* desc) {
        M::Entry e;
        e.levelId = id; e.name = name; e.description = desc;
        e.mapId = 0;            // Eastern Kingdoms
        e.areaId = 1519;        // Stormwind City
        e.levelIndex = levelIndex;
        e.minZ = minZ; e.maxZ = maxZ;
        e.texturePath = texPath;
        e.displayName = displayName;
        e.iconColorRGBA = packRgba(140, 200, 255);   // city blue
        c.entries.push_back(e);
    };
    add(1, "StormwindOldTown", 0, 0.0f, 80.0f,
        "Interface\\Minimap\\Stormwind\\OldTown.blp",
        "Old Town & Cathedral District",
        "Stormwind ground level - Z 0-80. Old Town, "
        "Trade District, Cathedral District, Mage Quarter "
        "are all at this base elevation.");
    add(2, "StormwindKeep", 1, 80.0f, 130.0f,
        "Interface\\Minimap\\Stormwind\\KeepRamp.blp",
        "Stormwind Keep Approach",
        "Stormwind Keep ramp + outer courtyard - Z "
        "80-130. Players above this elevation see the "
        "keep-approach overlay.");
    add(3, "StormwindThroneRoom", 2, 130.0f, 200.0f,
        "Interface\\Minimap\\Stormwind\\ThroneRoom.blp",
        "Throne Room & Royal Library",
        "Stormwind Keep upper level (Throne Room, Royal "
        "Library, secret tunnel) - Z 130-200. Highest "
        "elevation in city map.");
    return c;
}

WoweeMinimapLevels WoweeMinimapLevelsLoader::makeDalaran(
    const std::string& catalogName) {
    using M = WoweeMinimapLevels;
    WoweeMinimapLevels c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t levelIndex, float minZ, float maxZ,
                    const char* texPath, const char* displayName,
                    const char* desc) {
        M::Entry e;
        e.levelId = id; e.name = name; e.description = desc;
        e.mapId = 571;          // Northrend
        e.areaId = 4395;        // Dalaran
        e.levelIndex = levelIndex;
        e.minZ = minZ; e.maxZ = maxZ;
        e.texturePath = texPath;
        e.displayName = displayName;
        e.iconColorRGBA = packRgba(180, 140, 220);   // arcane purple
        c.entries.push_back(e);
    };
    add(100, "DalaranSewers", 0, 0.0f, 580.0f,
        "Interface\\Minimap\\Dalaran\\Sewers.blp",
        "The Underbelly",
        "Dalaran sewer level (The Underbelly) - Z "
        "0-580. Free-for-all PvP zone with rogue/druid "
        "stash.");
    add(101, "DalaranStreet", 1, 580.0f, 645.0f,
        "Interface\\Minimap\\Dalaran\\Street.blp",
        "Dalaran Street Level",
        "Dalaran main street - Z 580-645. Most "
        "vendors, Krasus' Landing, the Wonderworks. "
        "Default level players spawn into.");
    add(102, "DalaranAboveStreet", 2, 645.0f, 700.0f,
        "Interface\\Minimap\\Dalaran\\AboveStreet.blp",
        "Above Street",
        "Dalaran upper buildings (rooftops, arcane "
        "tower walkways) - Z 645-700. Reached via the "
        "few rooftop ladders.");
    add(103, "DalaranFloatingCathedral", 3, 700.0f, 800.0f,
        "Interface\\Minimap\\Dalaran\\Cathedral.blp",
        "Floating Cathedral",
        "Dalaran top-tier (Violet Citadel + Floating "
        "Cathedral) - Z 700-800. Highest reachable "
        "point in the city.");
    return c;
}

WoweeMinimapLevels WoweeMinimapLevelsLoader::makeUndercity(
    const std::string& catalogName) {
    using M = WoweeMinimapLevels;
    WoweeMinimapLevels c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t levelIndex, float minZ, float maxZ,
                    const char* texPath, const char* displayName,
                    const char* desc) {
        M::Entry e;
        e.levelId = id; e.name = name; e.description = desc;
        e.mapId = 0;            // Eastern Kingdoms
        e.areaId = 1497;        // Undercity
        e.levelIndex = levelIndex;
        e.minZ = minZ; e.maxZ = maxZ;
        e.texturePath = texPath;
        e.displayName = displayName;
        e.iconColorRGBA = packRgba(80, 60, 140);   // forsaken indigo
        c.entries.push_back(e);
    };
    // Undercity is the most vertically layered Eastern
    // Kingdoms capital - has 5 distinct minimap layers.
    add(200, "UndercitySewer", 0, -110.0f, -85.0f,
        "Interface\\Minimap\\Undercity\\Sewer.blp",
        "Sewer Outlet",
        "Undercity sewer outlet - Z -110 to -85. The "
        "lowest reachable point, where Tirisfal water "
        "drains.");
    add(201, "UndercityCanal", 1, -85.0f, -65.0f,
        "Interface\\Minimap\\Undercity\\Canal.blp",
        "Canal Walkway",
        "Undercity canal walkway - Z -85 to -65. "
        "Connects the Trade Quarter to the apothecary "
        "labs via narrow walkways.");
    add(202, "UndercityOuterRing", 2, -65.0f, -45.0f,
        "Interface\\Minimap\\Undercity\\OuterRing.blp",
        "Outer Ring",
        "Undercity outer ring - Z -65 to -45. The "
        "Trade Quarter, mailbox, auction house. Default "
        "Forsaken arrival point.");
    add(203, "UndercityInnerRing", 3, -45.0f, -20.0f,
        "Interface\\Minimap\\Undercity\\InnerRing.blp",
        "Inner Ring",
        "Undercity inner ring - Z -45 to -20. The four "
        "racial-trainer alcoves, Magic Quarter, "
        "Apothecarium ramp.");
    add(204, "UndercityThroneRoom", 4, -20.0f, 30.0f,
        "Interface\\Minimap\\Undercity\\Throne.blp",
        "Royal Quarter",
        "Undercity Royal Quarter (Throne Room) - Z "
        "-20 to 30. Sylvanas Windrunner's seat. Highest "
        "elevation, opens via the central elevator.");
    return c;
}

} // namespace pipeline
} // namespace wowee
