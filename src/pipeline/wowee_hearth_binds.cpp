#include "pipeline/wowee_hearth_binds.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'H', 'R', 'T'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".whrt";

} // namespace

const WoweeHearthBinds::Entry*
WoweeHearthBinds::findById(uint32_t bindId) const {
    for (const auto& e : entries)
        if (e.bindId == bindId) return &e;
    return nullptr;
}

std::vector<const WoweeHearthBinds::Entry*>
WoweeHearthBinds::findByFaction(uint8_t playerFaction) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.factionMask & playerFaction) out.push_back(&e);
    }
    return out;
}

std::vector<const WoweeHearthBinds::Entry*>
WoweeHearthBinds::findByMap(uint32_t mapId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.mapId == mapId) out.push_back(&e);
    return out;
}

bool WoweeHearthBindsLoader::save(const WoweeHearthBinds& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeHearthBinds::Entry& e) {
        writePOD(os, e.bindId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.areaId);
        writePOD(os, e.x);
        writePOD(os, e.y);
        writePOD(os, e.z);
        writePOD(os, e.facing);
        writePOD(os, e.npcId);
        writePOD(os, e.factionMask);
        writePOD(os, e.bindKind);
        writePOD(os, e.levelMin);
        writePOD(os, e.pad0);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeHearthBinds WoweeHearthBindsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeHearthBinds>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeHearthBinds::Entry& e) {
        if (!readPOD(is, e.bindId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.areaId) ||
            !readPOD(is, e.x) ||
            !readPOD(is, e.y) ||
            !readPOD(is, e.z) ||
            !readPOD(is, e.facing) ||
            !readPOD(is, e.npcId) ||
            !readPOD(is, e.factionMask) ||
            !readPOD(is, e.bindKind) ||
            !readPOD(is, e.levelMin) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeHearthBindsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeHearthBinds WoweeHearthBindsLoader::makeStarterCities(
    const std::string& catalogName) {
    using H = WoweeHearthBinds;
    WoweeHearthBinds c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint32_t area, float x, float y, float z, float f,
                    uint32_t npc, uint8_t faction,
                    const char* desc) {
        H::Entry e;
        e.bindId = id; e.name = name; e.description = desc;
        e.mapId = map; e.areaId = area;
        e.x = x; e.y = y; e.z = z; e.facing = f;
        e.npcId = npc;
        e.factionMask = faction;
        e.bindKind = H::Inn;
        e.levelMin = 1;
        e.iconColorRGBA = packRgba(255, 220, 100);   // gold inn
        c.entries.push_back(e);
    };
    // Eastern Kingdoms (mapId=0): Stormwind Inn (Old Town).
    add(1, "StormwindInn", 0, 1519,
        -8843.0f, 645.0f, 95.0f, 1.5f,
        6739, H::AllianceOnly,
        "Pig and Whistle Tavern - Stormwind Old Town. "
        "Allerian Holimion is the local innkeeper.");
    // Eastern Kingdoms: Ironforge Inn (Forlorn Cavern).
    add(2, "IronforgeInn", 0, 1537,
        -4862.0f, -872.0f, 502.0f, 4.7f,
        6741, H::AllianceOnly,
        "Stonefire Tavern - Ironforge Commons. Inn-keeper "
        "Firebrew serves the Wildhammer dwarves passing "
        "through.");
    // Kalimdor (mapId=1): Orgrimmar Inn (Valley of Strength).
    add(3, "OrgrimmarInn", 1, 1637,
        1665.0f, -4326.0f, 60.0f, 1.0f,
        6929, H::HordeOnly,
        "Wreckin' Ball Tavern - Valley of Strength. "
        "Innkeeper Gryshka serves the Horde travelers "
        "arriving from the eastern continents.");
    // Kalimdor: Thunder Bluff Inn (Lower Rise).
    add(4, "ThunderBluffInn", 1, 1638,
        -1290.0f, 161.0f, 130.0f, 4.7f,
        6746, H::HordeOnly,
        "Thunder Bluff Inn - Lower Rise. Innkeeper Pala "
        "serves the Tauren and visiting Horde.");
    return c;
}

WoweeHearthBinds WoweeHearthBindsLoader::makeCapitals(
    const std::string& catalogName) {
    using H = WoweeHearthBinds;
    WoweeHearthBinds c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint32_t area, float x, float y, float z, float f,
                    uint32_t npc, uint8_t faction,
                    const char* desc) {
        H::Entry e;
        e.bindId = id; e.name = name; e.description = desc;
        e.mapId = map; e.areaId = area;
        e.x = x; e.y = y; e.z = z; e.facing = f;
        e.npcId = npc;
        e.factionMask = faction;
        e.bindKind = H::Capital;
        e.levelMin = 10;
        e.iconColorRGBA = packRgba(140, 200, 255);   // capital blue
        c.entries.push_back(e);
    };
    add(100, "StormwindKeepBind", 0, 1519,
        -8866.0f, 671.0f, 97.0f, 1.5f,
        7232, H::AllianceOnly,
        "Stormwind Keep bind clerk - used by Alliance "
        "officers and quest chains that grant capital-bind "
        "as a reward.");
    add(101, "IronforgeBind", 0, 1537,
        -4924.0f, -955.0f, 501.0f, 0.0f,
        13283, H::AllianceOnly,
        "Ironforge royal hall bind clerk - used by dwarven "
        "Magni quest line.");
    add(102, "DarnassusBind", 1, 1657,
        9947.0f, 2516.0f, 1330.0f, 4.5f,
        7301, H::AllianceOnly,
        "Darnassus Temple of the Moon bind clerk - kaldorei "
        "lore quest line completion reward.");
    add(103, "OrgrimmarGrommashHold", 1, 1637,
        1633.0f, -4439.0f, 16.0f, 0.5f,
        7236, H::HordeOnly,
        "Orgrimmar Grommash Hold bind clerk - Horde "
        "officer hall, requires honored standing with "
        "Orgrimmar.");
    add(104, "UndercityBind", 0, 1497,
        1633.0f, 240.0f, -50.0f, 1.5f,
        13208, H::HordeOnly,
        "Undercity Royal Quarters bind clerk - Forsaken "
        "lore quest line reward.");
    add(105, "ThunderBluffBind", 1, 1638,
        -1271.0f, 80.0f, 128.0f, 5.0f,
        13284, H::HordeOnly,
        "Thunder Bluff High Rise bind clerk - Tauren "
        "elder lore quest reward.");
    return c;
}

WoweeHearthBinds WoweeHearthBindsLoader::makeStarterInns(
    const std::string& catalogName) {
    using H = WoweeHearthBinds;
    WoweeHearthBinds c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint32_t area, float x, float y, float z, float f,
                    uint32_t npc, uint8_t faction,
                    const char* desc) {
        H::Entry e;
        e.bindId = id; e.name = name; e.description = desc;
        e.mapId = map; e.areaId = area;
        e.x = x; e.y = y; e.z = z; e.facing = f;
        e.npcId = npc;
        e.factionMask = faction;
        e.bindKind = H::Inn;
        e.levelMin = 1;
        e.iconColorRGBA = packRgba(200, 160, 80);    // tavern brown
        c.entries.push_back(e);
    };
    // Alliance starter inns.
    add(200, "GoldshireLionsPride", 0, 9,
        -9460.0f, 64.0f, 56.0f, 0.0f,
        6740, H::AllianceOnly,
        "Lion's Pride Inn - Goldshire, Elwynn Forest. "
        "Innkeeper Farley serves the human starter zone.");
    add(201, "BrillGallowsEnd", 0, 85,
        2266.0f, 286.0f, 35.0f, 1.5f,
        6747, H::HordeOnly,
        "Gallows' End Tavern - Brill, Tirisfal Glades. "
        "Innkeeper Renee Renee serves the Forsaken "
        "starter zone.");
    add(202, "RazorHillInn", 1, 362,
        345.0f, -4710.0f, 16.0f, 0.0f,
        6748, H::HordeOnly,
        "Razor Hill Inn - Durotar. Innkeeper Grosk "
        "serves the orc/troll starter zone.");
    add(203, "BloodhoofVillageInn", 1, 222,
        -2370.0f, -370.0f, -10.0f, 4.5f,
        6929, H::HordeOnly,
        "Bloodhoof Village Inn - Mulgore. Innkeeper "
        "Kauth serves the Tauren starter zone.");
    add(204, "KharanosThunderBrew", 0, 132,
        -5605.0f, -480.0f, 400.0f, 1.5f,
        6735, H::AllianceOnly,
        "Thunderbrew Distillery - Kharanos, Dun Morogh. "
        "Innkeeper Belm serves the dwarf/gnome starter "
        "zone.");
    add(205, "AldrassilStarbreezeInn", 1, 188,
        10318.0f, 829.0f, 1326.0f, 1.0f,
        6736, H::AllianceOnly,
        "Starbreeze Village Inn - Teldrassil. Innkeeper "
        "Saelienne serves the night elf starter zone.");
    add(206, "ShadowglenInn", 1, 188,
        10311.0f, 822.0f, 1326.0f, 1.0f,
        6737, H::AllianceOnly,
        "Shadowglen Inn - Teldrassil. The first inn night "
        "elf characters can bind to (level 5+).");
    add(207, "SunRockRetreatInn", 1, 405,
        -2392.0f, -1992.0f, 95.0f, 0.5f,
        6738, H::HordeOnly,
        "Sun Rock Retreat Inn - Stonetalon Mountains. "
        "Innkeeper Heather serves the Tauren level 15-25 "
        "Horde travelers.");
    return c;
}

} // namespace pipeline
} // namespace wowee
