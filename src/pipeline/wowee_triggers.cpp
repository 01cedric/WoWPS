#include "pipeline/wowee_triggers.hpp"
#include "core/coordinates.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'R', 'G'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wtrg";

} // namespace

const WoweeTrigger::Entry* WoweeTrigger::findById(uint32_t triggerId) const {
    for (const auto& e : entries) if (e.triggerId == triggerId) return &e;
    return nullptr;
}

const char* WoweeTrigger::shapeName(uint8_t s) {
    switch (s) {
        case ShapeBox:    return "box";
        case ShapeSphere: return "sphere";
        default:          return "unknown";
    }
}

const char* WoweeTrigger::kindName(uint8_t k) {
    switch (k) {
        case KindTeleport:         return "teleport";
        case KindQuestExploration: return "quest-explore";
        case KindScript:           return "script";
        case KindInstanceEntrance: return "instance";
        case KindAreaName:         return "area-name";
        case KindCombatStartZone:  return "pvp-zone";
        case KindWaypoint:         return "waypoint";
        default:                   return "unknown";
    }
}

bool WoweeTriggerLoader::save(const WoweeTrigger& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeTrigger::Entry& e) {
        writePOD(os, e.triggerId);
        writePOD(os, e.mapId);
        writePOD(os, e.areaId);
        writeStr(os, e.name);
        writePOD(os, e.center.x);
        writePOD(os, e.center.y);
        writePOD(os, e.center.z);
        writePOD(os, e.shape);
        writePOD(os, e.kind);
        writePadding(os, 2);
        writePOD(os, e.boxDims.x);
        writePOD(os, e.boxDims.y);
        writePOD(os, e.boxDims.z);
        writePOD(os, e.radius);
        writePOD(os, e.actionTarget);
        writePOD(os, e.dest.x);
        writePOD(os, e.dest.y);
        writePOD(os, e.dest.z);
        writePOD(os, e.destOrientation);
        writePOD(os, e.requiredQuestId);
        writePOD(os, e.requiredItemId);
        writePOD(os, e.minLevel);
        writePadding(os, 2);
                       });
}

WoweeTrigger WoweeTriggerLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeTrigger>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeTrigger::Entry& e) {
        if (!readPOD(is, e.triggerId) ||
            !readPOD(is, e.mapId) ||
            !readPOD(is, e.areaId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.center.x) ||
            !readPOD(is, e.center.y) ||
            !readPOD(is, e.center.z) ||
            !readPOD(is, e.shape) ||
            !readPOD(is, e.kind)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
        if (!readPOD(is, e.boxDims.x) ||
            !readPOD(is, e.boxDims.y) ||
            !readPOD(is, e.boxDims.z) ||
            !readPOD(is, e.radius) ||
            !readPOD(is, e.actionTarget) ||
            !readPOD(is, e.dest.x) ||
            !readPOD(is, e.dest.y) ||
            !readPOD(is, e.dest.z) ||
            !readPOD(is, e.destOrientation) ||
            !readPOD(is, e.requiredQuestId) ||
            !readPOD(is, e.requiredItemId) ||
            !readPOD(is, e.minLevel)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
                                  return true;
                              });
}

bool WoweeTriggerLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeTrigger WoweeTriggerLoader::makeStarter(const std::string& catalogName) {
    WoweeTrigger c;
    c.name = catalogName;
    {
        WoweeTrigger::Entry e;
        e.triggerId = 1; e.mapId = 0; e.areaId = 87;   // WMS Goldshire
        e.name = "Goldshire entrance";
        e.center = {-9460.0f, 60.0f, 56.0f};
        e.shape = WoweeTrigger::ShapeSphere;
        e.radius = 60.0f;
        e.kind = WoweeTrigger::KindAreaName;
        c.entries.push_back(e);
    }
    {
        WoweeTrigger::Entry e;
        e.triggerId = 2; e.mapId = 0; e.areaId = 12;   // Elwynn Forest
        e.name = "Bandit Camp clearing";
        e.center = {-9700.0f, 50.0f, 200.0f};
        e.shape = WoweeTrigger::ShapeSphere;
        e.radius = 30.0f;
        e.kind = WoweeTrigger::KindQuestExploration;
        e.actionTarget = 100;          // matches WQT.makeChain quest id
        c.entries.push_back(e);
    }
    return c;
}

WoweeTrigger WoweeTriggerLoader::makeDungeon(const std::string& catalogName) {
    WoweeTrigger c;
    c.name = catalogName;
    // Outdoor approach area-name marker.
    {
        WoweeTrigger::Entry e;
        e.triggerId = 100; e.mapId = 0; e.areaId = 40;     // Westfall
        e.name = "Approaching Deadmines";
        e.center = {-11000.0f, 50.0f, 1500.0f};
        e.shape = WoweeTrigger::ShapeSphere;
        e.radius = 25.0f;
        e.kind = WoweeTrigger::KindAreaName;
        c.entries.push_back(e);
    }
    // Portal-style instance entrance with key requirement.
    {
        WoweeTrigger::Entry e;
        e.triggerId = 101; e.mapId = 0; e.areaId = 40;
        e.name = "Deadmines Portal";
        e.center = {-11200.0f, 60.0f, 1600.0f};
        e.shape = WoweeTrigger::ShapeBox;
        e.boxDims = {3.0f, 5.0f, 3.0f};
        e.kind = WoweeTrigger::KindInstanceEntrance;
        e.actionTarget = 36;            // WMS Deadmines mapId
        e.dest = {-15.0f, 20.0f, 0.0f};
        e.destOrientation = 0.0f;
        e.requiredItemId = 5200;        // matches WLCK.makeDungeon "Boss Vault Seal" key
        e.minLevel = 17;
        c.entries.push_back(e);
    }
    // Inside-instance exit teleport back to the outdoor portal.
    {
        WoweeTrigger::Entry e;
        e.triggerId = 102; e.mapId = 36; e.areaId = 0;
        e.name = "Deadmines Exit";
        e.center = {-15.0f, 20.0f, 5.0f};
        e.shape = WoweeTrigger::ShapeBox;
        e.boxDims = {3.0f, 5.0f, 3.0f};
        e.kind = WoweeTrigger::KindTeleport;
        e.actionTarget = 0;              // back to Eastern Kingdoms
        e.dest = {-11200.0f, 60.0f, 1605.0f};
        e.destOrientation = core::coords::PI;  // facing south on exit
        c.entries.push_back(e);
    }
    return c;
}

WoweeTrigger WoweeTriggerLoader::makeFlightPath(const std::string& catalogName) {
    WoweeTrigger c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t mapId, uint32_t areaId,
                    const char* name, glm::vec3 center) {
        WoweeTrigger::Entry e;
        e.triggerId = id; e.mapId = mapId; e.areaId = areaId;
        e.name = name; e.center = center;
        e.shape = WoweeTrigger::ShapeSphere; e.radius = 8.0f;
        e.kind = WoweeTrigger::KindWaypoint;
        c.entries.push_back(e);
    };
    add(200, 0, 1,  "Stormwind Gryphon Master proximity",
        {-9000.0f, 100.0f, 50.0f});
    add(201, 0, 87, "Goldshire Gryphon Master proximity",
        {-9460.0f, 60.0f, 56.0f});
    return c;
}

} // namespace pipeline
} // namespace wowee
