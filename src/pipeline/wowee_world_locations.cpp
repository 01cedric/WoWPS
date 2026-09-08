#include "pipeline/wowee_world_locations.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'O', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wloc";

} // namespace

const WoweeWorldLocations::Entry*
WoweeWorldLocations::findById(uint32_t locationId) const {
    for (const auto& e : entries)
        if (e.locationId == locationId) return &e;
    return nullptr;
}

std::vector<const WoweeWorldLocations::Entry*>
WoweeWorldLocations::findByMap(uint32_t mapId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.mapId == mapId) out.push_back(&e);
    return out;
}

std::vector<const WoweeWorldLocations::Entry*>
WoweeWorldLocations::findByMapAndKind(uint32_t mapId,
                                         uint8_t kind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.mapId == mapId && e.locKind == kind)
            out.push_back(&e);
    return out;
}

bool WoweeWorldLocationsLoader::save(
    const WoweeWorldLocations& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.locationId);
        writeStr(os, e.name);
        writePOD(os, e.mapId);
        writePOD(os, e.areaId);
        writePOD(os, e.x);
        writePOD(os, e.y);
        writePOD(os, e.z);
        writePOD(os, e.locKind);
        writePOD(os, e.iconIndex);
        writePOD(os, e.factionAccess);
        writePOD(os, e.pad0);
        writePOD(os, e.respawnSec);
        writePOD(os, e.discoverableXp);
        writePOD(os, e.requiredSkillId);
        writePOD(os, e.requiredSkillLevel);
    });
}

WoweeWorldLocations WoweeWorldLocationsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeWorldLocations>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeWorldLocations::Entry& e) {
        if (!readPOD(is, e.locationId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.areaId) ||
            !readPOD(is, e.x) ||
            !readPOD(is, e.y) ||
            !readPOD(is, e.z) ||
            !readPOD(is, e.locKind) ||
            !readPOD(is, e.iconIndex) ||
            !readPOD(is, e.factionAccess) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.respawnSec) ||
            !readPOD(is, e.discoverableXp) ||
            !readPOD(is, e.requiredSkillId) ||
            !readPOD(is, e.requiredSkillLevel)) { return false; }
                                  return true;
                              });
}

bool WoweeWorldLocationsLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeWorldLocations::Entry makeLoc(uint32_t locationId,
                                      const char* name,
                                      uint32_t mapId,
                                      uint32_t areaId,
                                      float x, float y, float z,
                                      uint8_t locKind,
                                      uint8_t iconIndex,
                                      uint8_t factionAccess,
                                      uint32_t respawnSec,
                                      uint32_t discoverableXp,
                                      uint16_t requiredSkillId,
                                      uint16_t requiredSkillLevel) {
    WoweeWorldLocations::Entry e;
    e.locationId = locationId; e.name = name;
    e.mapId = mapId; e.areaId = areaId;
    e.x = x; e.y = y; e.z = z;
    e.locKind = locKind;
    e.iconIndex = iconIndex;
    e.factionAccess = factionAccess;
    e.respawnSec = respawnSec;
    e.discoverableXp = discoverableXp;
    e.requiredSkillId = requiredSkillId;
    e.requiredSkillLevel = requiredSkillLevel;
    return e;
}

} // namespace

WoweeWorldLocations WoweeWorldLocationsLoader::makeAlliancePOIs(
    const std::string& catalogName) {
    using L = WoweeWorldLocations;
    WoweeWorldLocations c;
    c.name = catalogName;
    // Stormwind: areaId 1519, Eastern Kingdoms map=0.
    c.entries.push_back(makeLoc(
        1, "Stormwind City", 0, 1519,
        -8836.f, 626.f, 94.f,
        L::POI, 12, L::Alliance,
        0, 100, 0, 0));
    // Ironforge: areaId 1537.
    c.entries.push_back(makeLoc(
        2, "Ironforge", 0, 1537,
        -4925.f, -956.f, 502.f,
        L::POI, 12, L::Alliance,
        0, 100, 0, 0));
    // Goldshire: areaId 87, lower XP (small village).
    c.entries.push_back(makeLoc(
        3, "Goldshire", 0, 87,
        -9460.f, 53.f, 56.f,
        L::POI, 12, L::Alliance,
        0, 50, 0, 0));
    // Sentinel Hill: Westfall area 957.
    c.entries.push_back(makeLoc(
        4, "Sentinel Hill", 0, 957,
        -10620.f, 1067.f, 53.f,
        L::POI, 12, L::Alliance,
        0, 80, 0, 0));
    return c;
}

WoweeWorldLocations WoweeWorldLocationsLoader::makeHerbalismNodes(
    const std::string& catalogName) {
    using L = WoweeWorldLocations;
    WoweeWorldLocations c;
    c.name = catalogName;
    // Peacebloom: skill 1 (lowest), Elwynn area=12.
    // 600s respawn typical for herb nodes.
    c.entries.push_back(makeLoc(
        100, "Peacebloom Patch", 0, 12,
        -9020.f, 130.f, 75.f,
        L::HerbNode, 5, L::Both,
        600, 0, 182 /* Herbalism */, 1));
    // Silverleaf: skill 1, same zone.
    c.entries.push_back(makeLoc(
        101, "Silverleaf Patch", 0, 12,
        -8860.f, 200.f, 78.f,
        L::HerbNode, 5, L::Both,
        600, 0, 182, 1));
    // Briarthorn: skill 70, Westfall area=40.
    c.entries.push_back(makeLoc(
        102, "Briarthorn Bush", 0, 40,
        -10510.f, 950.f, 50.f,
        L::HerbNode, 5, L::Both,
        600, 0, 182, 70));
    // Mageroyal: skill 50, Westfall.
    c.entries.push_back(makeLoc(
        103, "Mageroyal Patch", 0, 40,
        -10780.f, 1130.f, 51.f,
        L::HerbNode, 5, L::Both,
        600, 0, 182, 50));
    // Stranglekelp: skill 85, fishing-coast in
    // Westfall.
    c.entries.push_back(makeLoc(
        104, "Stranglekelp Cluster", 0, 40,
        -11050.f, 1700.f, 4.f,
        L::HerbNode, 5, L::Both,
        600, 0, 182, 85));
    return c;
}

WoweeWorldLocations WoweeWorldLocationsLoader::makeRareSpawns(
    const std::string& catalogName) {
    using L = WoweeWorldLocations;
    WoweeWorldLocations c;
    c.name = catalogName;
    // Mor'Ladim: Duskwood (area=10), 30..120min spawn
    // (1800..7200s; using 3600 = 1hr midpoint).
    c.entries.push_back(makeLoc(
        200, "Mor'Ladim", 0, 10,
        -10770.f, -1700.f, 50.f,
        L::RareSpawn, 6, L::Both,
        3600, 0, 0, 0));
    // Princess Tempestria: Winterspring elemental
    // rare (area=618), Kalimdor map=1.
    c.entries.push_back(makeLoc(
        201, "Princess Tempestria", 1, 618,
        6020.f, -4450.f, 800.f,
        L::RareSpawn, 6, L::Both,
        7200, 0, 0, 0));
    // Foreman Rigger: Stranglethorn (area=33).
    c.entries.push_back(makeLoc(
        202, "Foreman Rigger", 0, 33,
        -13620.f, 870.f, 44.f,
        L::RareSpawn, 6, L::Both,
        1800, 0, 0, 0));
    // Lord Sakrasis: STV satyr camp.
    c.entries.push_back(makeLoc(
        203, "Lord Sakrasis", 0, 33,
        -12230.f, 90.f, 9.f,
        L::RareSpawn, 6, L::Both,
        3600, 0, 0, 0));
    return c;
}

} // namespace pipeline
} // namespace wowee
