#include "pipeline/wowee_expansion_names.hpp"
#include "pipeline/wowee_lfg.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'F', 'G'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wlfg";

} // namespace

const WoweeLFGDungeon::Entry*
WoweeLFGDungeon::findById(uint32_t dungeonId) const {
    for (const auto& e : entries)
        if (e.dungeonId == dungeonId) return &e;
    return nullptr;
}

const char* WoweeLFGDungeon::difficultyName(uint8_t d) {
    switch (d) {
        case Normal:   return "normal";
        case Heroic:   return "heroic";
        case Mythic:   return "mythic";
        case Hardmode: return "hardmode";
        default:       return "unknown";
    }
}

const char* WoweeLFGDungeon::expansionRequiredName(uint8_t e) {
    // The word is the sidecar's, shared with the other two formats
    // that gate on an expansion and with the importers that read them.
    return expansionName(e);
}

bool WoweeLFGDungeonLoader::save(const WoweeLFGDungeon& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeLFGDungeon::Entry& e) {
        writePOD(os, e.dungeonId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.recommendedLevel);
        writePOD(os, e.minGearLevel);
        writePOD(os, e.difficulty);
        writePOD(os, e.groupSize);
        writePOD(os, e.requiredRolesMask);
        writePOD(os, e.expansionRequired);
        writePOD(os, e.queueRewardItemId);
        writePOD(os, e.queueRewardEmblemCount);
        writePadding(os, 2);
        writePOD(os, e.firstClearAchievement);
                       });
}

WoweeLFGDungeon WoweeLFGDungeonLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeLFGDungeon>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeLFGDungeon::Entry& e) {
        if (!readPOD(is, e.dungeonId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.recommendedLevel) ||
            !readPOD(is, e.minGearLevel) ||
            !readPOD(is, e.difficulty) ||
            !readPOD(is, e.groupSize) ||
            !readPOD(is, e.requiredRolesMask) ||
            !readPOD(is, e.expansionRequired) ||
            !readPOD(is, e.queueRewardItemId) ||
            !readPOD(is, e.queueRewardEmblemCount)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
        if (!readPOD(is, e.firstClearAchievement)) { return false; }
                                  return true;
                              });
}

bool WoweeLFGDungeonLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeLFGDungeon WoweeLFGDungeonLoader::makeStarter(
    const std::string& catalogName) {
    WoweeLFGDungeon c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t mapId,
                    uint16_t minL, uint16_t maxL, uint16_t recL,
                    const char* desc) {
        WoweeLFGDungeon::Entry e;
        e.dungeonId = id; e.name = name; e.description = desc;
        e.mapId = mapId;
        e.minLevel = minL; e.maxLevel = maxL;
        e.recommendedLevel = recL;
        e.difficulty = WoweeLFGDungeon::Normal;
        e.groupSize = 5;
        e.expansionRequired = WoweeLFGDungeon::Classic;
        c.entries.push_back(e);
    };
    add(1, "Ragefire Chasm",  389, 13, 18, 15,
        "Volcanic 5-man dungeon under Orgrimmar.");
    add(2, "Wailing Caverns", 43,  17, 24, 20,
        "Druidic 5-man dungeon in the Barrens.");
    add(3, "The Deadmines",   36,  18, 23, 20,
        "Defias hideout 5-man dungeon in Westfall.");
    return c;
}

WoweeLFGDungeon WoweeLFGDungeonLoader::makeHeroic(
    const std::string& catalogName) {
    WoweeLFGDungeon c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t mapId,
                    uint16_t minIlvl, uint16_t emblemCount,
                    uint32_t ach, const char* desc) {
        WoweeLFGDungeon::Entry e;
        e.dungeonId = id; e.name = name; e.description = desc;
        e.mapId = mapId;
        e.minLevel = 80; e.maxLevel = 80;
        e.recommendedLevel = 80;
        e.minGearLevel = minIlvl;
        e.difficulty = WoweeLFGDungeon::Heroic;
        e.groupSize = 5;
        e.expansionRequired = WoweeLFGDungeon::WotLK;
        e.queueRewardEmblemCount = emblemCount;
        e.firstClearAchievement = ach;
        c.entries.push_back(e);
    };
    add(100, "Halls of Lightning Heroic",  602, 180, 2, 1862,
        "Storm titan-keeper 5-man - Loken finale.");
    add(101, "Halls of Stone Heroic",      599, 180, 2, 1865,
        "Iron dwarf 5-man - Tribunal of Ages event.");
    add(102, "Utgarde Pinnacle Heroic",    575, 180, 2, 1487,
        "Vrykul 5-man - King Ymiron finale.");
    add(103, "The Violet Hold Heroic",     608, 180, 2, 1816,
        "Dalaran prison breakout 5-man.");
    add(104, "Old Kingdom Heroic",         595, 180, 2, 1860,
        "Faceless ones 5-man - Herald Volazj.");
    return c;
}

WoweeLFGDungeon WoweeLFGDungeonLoader::makeRaid(
    const std::string& catalogName) {
    WoweeLFGDungeon c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t mapId,
                    uint8_t difficulty, uint8_t groupSize,
                    uint16_t minIlvl, uint16_t emblemCount,
                    uint32_t ach, const char* desc) {
        WoweeLFGDungeon::Entry e;
        e.dungeonId = id; e.name = name; e.description = desc;
        e.mapId = mapId;
        e.minLevel = 80; e.maxLevel = 80;
        e.recommendedLevel = 80;
        e.minGearLevel = minIlvl;
        e.difficulty = difficulty;
        e.groupSize = groupSize;
        e.expansionRequired = WoweeLFGDungeon::WotLK;
        e.queueRewardEmblemCount = emblemCount;
        e.firstClearAchievement = ach;
        c.entries.push_back(e);
    };
    add(200, "Naxxramas-25",          533,
        WoweeLFGDungeon::Normal,   25, 200,  5, 1996,
        "25-man recycled tier-3 raid in Northrend.");
    add(201, "Ulduar-25 Hardmode",    603,
        WoweeLFGDungeon::Hardmode, 25, 220,  5, 2200,
        "25-man with toggleable hardmode boss difficulty.");
    add(202, "Trial of the Crusader-25", 649,
        WoweeLFGDungeon::Mythic,   25, 232, 10, 4047,
        "25-man Argent Crusade raid, mythic difficulty.");
    return c;
}

} // namespace pipeline
} // namespace wowee
