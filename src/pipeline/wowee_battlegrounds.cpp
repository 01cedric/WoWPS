#include "pipeline/wowee_battlegrounds.hpp"
#include "core/coordinates.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'G', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wbgd";

} // namespace

const WoweeBattleground::Entry*
WoweeBattleground::findById(uint32_t bgId) const {
    for (const auto& e : entries) if (e.battlegroundId == bgId) return &e;
    return nullptr;
}

const char* WoweeBattleground::objectiveKindName(uint8_t k) {
    switch (k) {
        case Annihilation: return "annihilation";
        case CaptureFlag:  return "ctf";
        case ControlNodes: return "nodes";
        case KingOfHill:   return "koh";
        case ResourceRace: return "resource-race";
        case CarryObject:  return "carry-object";
        default:           return "unknown";
    }
}

bool WoweeBattlegroundLoader::save(const WoweeBattleground& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeBattleground::Entry& e) {
        writePOD(os, e.battlegroundId);
        writePOD(os, e.mapId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.objectiveKind);
        writePOD(os, e.minPlayersPerSide);
        writePOD(os, e.maxPlayersPerSide);
        writePadding(os, 1);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.scoreToWin);
        writePOD(os, e.timeLimitSeconds);
        writePOD(os, e.bracketSize);
        writePadding(os, 3);
        writePOD(os, e.allianceStart.x);
        writePOD(os, e.allianceStart.y);
        writePOD(os, e.allianceStart.z);
        writePOD(os, e.allianceFacing);
        writePOD(os, e.hordeStart.x);
        writePOD(os, e.hordeStart.y);
        writePOD(os, e.hordeStart.z);
        writePOD(os, e.hordeFacing);
        writePOD(os, e.respawnTimeSeconds);
        writePadding(os, 2);
        writePOD(os, e.markTokenId);
                       });
}

WoweeBattleground WoweeBattlegroundLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeBattleground>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeBattleground::Entry& e) {
        if (!readPOD(is, e.battlegroundId) ||
            !readPOD(is, e.mapId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.objectiveKind) ||
            !readPOD(is, e.minPlayersPerSide) ||
            !readPOD(is, e.maxPlayersPerSide)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.scoreToWin) ||
            !readPOD(is, e.timeLimitSeconds) ||
            !readPOD(is, e.bracketSize)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.allianceStart.x) ||
            !readPOD(is, e.allianceStart.y) ||
            !readPOD(is, e.allianceStart.z) ||
            !readPOD(is, e.allianceFacing) ||
            !readPOD(is, e.hordeStart.x) ||
            !readPOD(is, e.hordeStart.y) ||
            !readPOD(is, e.hordeStart.z) ||
            !readPOD(is, e.hordeFacing) ||
            !readPOD(is, e.respawnTimeSeconds)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
        if (!readPOD(is, e.markTokenId)) { return false; }
                                  return true;
                              });
}

bool WoweeBattlegroundLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeBattleground WoweeBattlegroundLoader::makeStarter(const std::string& catalogName) {
    WoweeBattleground c;
    c.name = catalogName;
    {
        WoweeBattleground::Entry e;
        e.battlegroundId = 1; e.mapId = 0;
        e.name = "Mountain Crown";
        e.description = "Hold the central peak. Three captures to win.";
        e.objectiveKind = WoweeBattleground::KingOfHill;
        e.minPlayersPerSide = 5; e.maxPlayersPerSide = 10;
        e.scoreToWin = 3; e.timeLimitSeconds = 1800;
        e.allianceStart = {-100.0f, 50.0f, 0.0f}; e.allianceFacing = 0.0f;
        e.hordeStart = {100.0f, 50.0f, 0.0f}; e.hordeFacing = core::coords::PI;
        c.entries.push_back(e);
    }
    return c;
}

WoweeBattleground WoweeBattlegroundLoader::makeClassic(const std::string& catalogName) {
    WoweeBattleground c;
    c.name = catalogName;
    {
        WoweeBattleground::Entry e;
        e.battlegroundId = 489; e.mapId = 489;
        e.name = "Warsong Gulch";
        e.description = "Capture the enemy flag and return it 3 times.";
        e.objectiveKind = WoweeBattleground::CaptureFlag;
        e.minPlayersPerSide = 8; e.maxPlayersPerSide = 10;
        e.minLevel = 10; e.maxLevel = 80;
        e.scoreToWin = 3; e.timeLimitSeconds = 1800;
        // markTokenId 102 matches WTKN.makePvp's
        // "Mark of Honor: Warsong Gulch".
        e.markTokenId = 102;
        c.entries.push_back(e);
    }
    {
        WoweeBattleground::Entry e;
        e.battlegroundId = 529; e.mapId = 529;
        e.name = "Arathi Basin";
        e.description = "Control 5 nodes to harvest 1600 resources.";
        e.objectiveKind = WoweeBattleground::ControlNodes;
        e.minPlayersPerSide = 12; e.maxPlayersPerSide = 15;
        e.minLevel = 20; e.maxLevel = 80;
        e.scoreToWin = 1600; e.timeLimitSeconds = 1500;
        e.markTokenId = 103;
        c.entries.push_back(e);
    }
    {
        WoweeBattleground::Entry e;
        e.battlegroundId = 30; e.mapId = 30;
        e.name = "Alterac Valley";
        e.description = "Eliminate the opposing General. Reinforcements: 600.";
        e.objectiveKind = WoweeBattleground::ResourceRace;
        e.minPlayersPerSide = 30; e.maxPlayersPerSide = 40;
        e.minLevel = 51; e.maxLevel = 80;
        e.scoreToWin = 600; e.timeLimitSeconds = 0;   // no time limit
        e.markTokenId = 104;
        c.entries.push_back(e);
    }
    return c;
}

WoweeBattleground WoweeBattlegroundLoader::makeArena(const std::string& catalogName) {
    WoweeBattleground c;
    c.name = catalogName;
    auto add = [&](uint32_t bgId, uint32_t mapId, const char* name,
                    uint8_t minPlayers, uint8_t maxPlayers) {
        WoweeBattleground::Entry e;
        e.battlegroundId = bgId; e.mapId = mapId;
        e.name = name;
        e.description = "Annihilation arena.";
        e.objectiveKind = WoweeBattleground::Annihilation;
        e.minPlayersPerSide = minPlayers; e.maxPlayersPerSide = maxPlayers;
        e.minLevel = 80; e.maxLevel = 80;
        e.scoreToWin = 1; e.timeLimitSeconds = 1500;   // 25 min cap
        e.bracketSize = 1;
        e.respawnTimeSeconds = 0;                       // no respawn
        c.entries.push_back(e);
    };
    add(559, 559, "Nagrand Arena (2v2)", 2, 2);
    add(562, 562, "Blade's Edge Arena (3v3)", 3, 3);
    add(572, 572, "Ruins of Lordaeron (5v5)", 5, 5);
    return c;
}

} // namespace pipeline
} // namespace wowee
