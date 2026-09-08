#include "pipeline/wowee_mage_portals.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'R', 'T'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wprt";

} // namespace

const WoweeMagePortals::Entry*
WoweeMagePortals::findById(uint32_t portalId) const {
    for (const auto& e : entries)
        if (e.portalId == portalId) return &e;
    return nullptr;
}

const WoweeMagePortals::Entry*
WoweeMagePortals::findBySpellId(uint32_t spellId) const {
    for (const auto& e : entries)
        if (e.spellId == spellId) return &e;
    return nullptr;
}

std::vector<const WoweeMagePortals::Entry*>
WoweeMagePortals::findByFaction(uint8_t faction) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.factionAccess == Both ||
            e.factionAccess == Neutral ||
            (faction != Both && e.factionAccess == faction)) {
            out.push_back(&e);
        }
    }
    return out;
}

bool WoweeMagePortalsLoader::save(const WoweeMagePortals& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeMagePortals::Entry& e) {
        writePOD(os, e.portalId);
        writePOD(os, e.spellId);
        writeStr(os, e.destinationName);
        writePOD(os, e.destX);
        writePOD(os, e.destY);
        writePOD(os, e.destZ);
        writePOD(os, e.destOrientation);
        writePOD(os, e.destMapId);
        writePOD(os, e.factionAccess);
        writePOD(os, e.portalKind);
        writePOD(os, e.levelRequirement);
        writePOD(os, e.reagentCount);
        writePOD(os, e.reagentItemId);
                       });
}

WoweeMagePortals WoweeMagePortalsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeMagePortals>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeMagePortals::Entry& e) {
        if (!readPOD(is, e.portalId) ||
            !readPOD(is, e.spellId)) { return false; }
        if (!readStr(is, e.destinationName)) { return false; }
        if (!readPOD(is, e.destX) ||
            !readPOD(is, e.destY) ||
            !readPOD(is, e.destZ) ||
            !readPOD(is, e.destOrientation) ||
            !readPOD(is, e.destMapId) ||
            !readPOD(is, e.factionAccess) ||
            !readPOD(is, e.portalKind) ||
            !readPOD(is, e.levelRequirement) ||
            !readPOD(is, e.reagentCount) ||
            !readPOD(is, e.reagentItemId)) { return false; }
                                  return true;
                              });
}

bool WoweeMagePortalsLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeMagePortals WoweeMagePortalsLoader::makeAllianceCities(
    const std::string& catalogName) {
    using P = WoweeMagePortals;
    WoweeMagePortals c;
    c.name = catalogName;
    auto add = [&](uint32_t pid, uint32_t spellId,
                    const char* destName,
                    float x, float y, float z, float o,
                    uint32_t mapId, uint8_t levelReq) {
        P::Entry e;
        e.portalId = pid; e.spellId = spellId;
        e.destinationName = destName;
        e.destX = x; e.destY = y; e.destZ = z;
        e.destOrientation = o;
        e.destMapId = mapId;
        e.factionAccess = P::Alliance;
        e.portalKind = P::Portal;
        e.levelRequirement = levelReq;
        e.reagentCount = 1;
        e.reagentItemId = 17032;       // Rune of
                                         //  Portals
        c.entries.push_back(e);
    };
    // Vanilla mage portal coords (capital city
    // portal-room or central plaza). Coords sourced
    // from the canonical Stormwind, Ironforge,
    // Darnassus and Theramore portal landing zones.
    add(1, 10059, "Stormwind",
        -9009.f, 873.f, 148.f, 0.f, 0, 40);
    add(2, 11416, "Ironforge",
        -4623.f, -915.f, 502.f, 0.f, 0, 40);
    add(3, 11419, "Darnassus",
        9982.f, 2300.f, 1330.f, 0.f, 1, 40);
    add(4, 49361, "Theramore",
        -3753.f, -4527.f, 9.f, 0.f, 1, 40);
    return c;
}

WoweeMagePortals WoweeMagePortalsLoader::makeHordeCities(
    const std::string& catalogName) {
    using P = WoweeMagePortals;
    WoweeMagePortals c;
    c.name = catalogName;
    auto add = [&](uint32_t pid, uint32_t spellId,
                    const char* destName,
                    float x, float y, float z, float o,
                    uint32_t mapId) {
        P::Entry e;
        e.portalId = pid; e.spellId = spellId;
        e.destinationName = destName;
        e.destX = x; e.destY = y; e.destZ = z;
        e.destOrientation = o;
        e.destMapId = mapId;
        e.factionAccess = P::Horde;
        e.portalKind = P::Portal;
        e.levelRequirement = 40;
        e.reagentCount = 1;
        e.reagentItemId = 17032;       // Rune of
                                         //  Portals
        c.entries.push_back(e);
    };
    add(10, 11417, "Orgrimmar",
        1576.f, -4453.f, 16.f, 0.f, 1);
    add(11, 11418, "Undercity",
        1830.f, 239.f, 60.f, 0.f, 0);
    add(12, 11420, "Thunder Bluff",
        -1277.f, 122.f, 132.f, 0.f, 1);
    return c;
}

WoweeMagePortals WoweeMagePortalsLoader::makeTeleports(
    const std::string& catalogName) {
    using P = WoweeMagePortals;
    WoweeMagePortals c;
    c.name = catalogName;
    auto addT = [&](uint32_t pid, uint32_t spellId,
                     const char* destName,
                     float x, float y, float z, float o,
                     uint32_t mapId, uint8_t faction,
                     uint8_t levelReq) {
        P::Entry e;
        e.portalId = pid; e.spellId = spellId;
        e.destinationName = destName;
        e.destX = x; e.destY = y; e.destZ = z;
        e.destOrientation = o;
        e.destMapId = mapId;
        e.factionAccess = faction;
        e.portalKind = P::Teleport;    // self-only,
                                         //  costs Rune of
                                         //  Teleportation
        e.levelRequirement = levelReq;
        e.reagentCount = 1;
        e.reagentItemId = 17031;       // Rune of
                                         //  Teleportation
                                         //  (NOT Rune of
                                         //  Portals)
        c.entries.push_back(e);
    };
    addT(20, 3561, "Stormwind",
         -9009.f, 873.f, 148.f, 0.f, 0,
         P::Alliance, 20);
    addT(21, 3562, "Ironforge",
         -4623.f, -915.f, 502.f, 0.f, 0,
         P::Alliance, 20);
    addT(22, 3567, "Orgrimmar",
         1576.f, -4453.f, 16.f, 0.f, 1,
         P::Horde, 20);
    return c;
}

} // namespace pipeline
} // namespace wowee
