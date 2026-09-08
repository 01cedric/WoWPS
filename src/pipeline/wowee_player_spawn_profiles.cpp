#include "pipeline/wowee_player_spawn_profiles.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'S', 'P'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wpsp";

// Race bits, mirroring WCHC layout. Used by presets.
constexpr uint32_t RACE_HUMAN     = 1u << 0;
constexpr uint32_t RACE_ORC       = 1u << 1;
constexpr uint32_t RACE_DWARF     = 1u << 2;
constexpr uint32_t RACE_NIGHTELF  = 1u << 3;
constexpr uint32_t RACE_UNDEAD    = 1u << 4;
constexpr uint32_t RACE_TAUREN    = 1u << 5;
constexpr uint32_t RACE_GNOME     = 1u << 6;
constexpr uint32_t RACE_TROLL     = 1u << 7;
constexpr uint32_t RACE_BLOODELF  = 1u << 9;
constexpr uint32_t RACE_DRAENEI   = 1u << 10;

// Class bits, mirroring WCHC layout. Used by presets. Kept complete (with
// [[maybe_unused]] on the unreferenced ones) so the bit layout is
// documented in code rather than only on the wiki.
constexpr uint32_t CLS_WARRIOR    = 1u << 0;
[[maybe_unused]] constexpr uint32_t CLS_PALADIN = 1u << 1;
constexpr uint32_t CLS_HUNTER     = 1u << 2;
constexpr uint32_t CLS_PRIEST     = 1u << 4;
constexpr uint32_t CLS_DK         = 1u << 5;
constexpr uint32_t CLS_SHAMAN     = 1u << 6;
constexpr uint32_t CLS_MAGE       = 1u << 7;
constexpr uint32_t CLS_DRUID      = 1u << 10;

} // namespace

const WoweePlayerSpawnProfile::Entry*
WoweePlayerSpawnProfile::findById(uint32_t profileId) const {
    for (const auto& e : entries)
        if (e.profileId == profileId) return &e;
    return nullptr;
}

const WoweePlayerSpawnProfile::Entry*
WoweePlayerSpawnProfile::findByRaceClass(uint32_t raceBit,
                                          uint32_t classBit) const {
    for (const auto& e : entries) {
        if ((e.raceMask & raceBit) && (e.classMask & classBit))
            return &e;
    }
    return nullptr;
}

bool WoweePlayerSpawnProfileLoader::save(
    const WoweePlayerSpawnProfile& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.profileId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.raceMask);
        writePOD(os, e.classMask);
        writePOD(os, e.mapId);
        writePOD(os, e.zoneId);
        writePOD(os, e.spawnX);
        writePOD(os, e.spawnY);
        writePOD(os, e.spawnZ);
        writePOD(os, e.spawnFacing);
        writePOD(os, e.bindMapId);
        writePOD(os, e.bindZoneId);
        writePOD(os, e.startingItem1Id);
        writePOD(os, e.startingItem1Count);
        writePOD(os, e.startingItem2Id);
        writePOD(os, e.startingItem2Count);
        writePOD(os, e.startingItem3Id);
        writePOD(os, e.startingItem3Count);
        writePOD(os, e.startingItem4Id);
        writePOD(os, e.startingItem4Count);
        writePOD(os, e.startingSpell1Id);
        writePOD(os, e.startingSpell2Id);
        writePOD(os, e.startingSpell3Id);
        writePOD(os, e.startingSpell4Id);
        writePOD(os, e.startingLevel);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
    });
}

WoweePlayerSpawnProfile WoweePlayerSpawnProfileLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweePlayerSpawnProfile>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweePlayerSpawnProfile::Entry& e) {
        if (!readPOD(is, e.profileId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.raceMask) ||
            !readPOD(is, e.classMask) ||
            !readPOD(is, e.mapId) ||
            !readPOD(is, e.zoneId) ||
            !readPOD(is, e.spawnX) ||
            !readPOD(is, e.spawnY) ||
            !readPOD(is, e.spawnZ) ||
            !readPOD(is, e.spawnFacing) ||
            !readPOD(is, e.bindMapId) ||
            !readPOD(is, e.bindZoneId) ||
            !readPOD(is, e.startingItem1Id) ||
            !readPOD(is, e.startingItem1Count) ||
            !readPOD(is, e.startingItem2Id) ||
            !readPOD(is, e.startingItem2Count) ||
            !readPOD(is, e.startingItem3Id) ||
            !readPOD(is, e.startingItem3Count) ||
            !readPOD(is, e.startingItem4Id) ||
            !readPOD(is, e.startingItem4Count) ||
            !readPOD(is, e.startingSpell1Id) ||
            !readPOD(is, e.startingSpell2Id) ||
            !readPOD(is, e.startingSpell3Id) ||
            !readPOD(is, e.startingSpell4Id) ||
            !readPOD(is, e.startingLevel) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweePlayerSpawnProfileLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweePlayerSpawnProfile WoweePlayerSpawnProfileLoader::makeAlliance(
    const std::string& catalogName) {
    using P = WoweePlayerSpawnProfile;
    WoweePlayerSpawnProfile c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t race,
                    uint32_t cls, uint32_t map, uint32_t zone,
                    float x, float y, float z, float facing,
                    uint32_t i1, uint32_t i2, uint32_t i3,
                    uint32_t s1, uint32_t s2, const char* desc) {
        P::Entry e;
        e.profileId = id; e.name = name; e.description = desc;
        e.raceMask = race; e.classMask = cls;
        e.mapId = map; e.zoneId = zone;
        e.spawnX = x; e.spawnY = y; e.spawnZ = z;
        e.spawnFacing = facing;
        e.bindMapId = map; e.bindZoneId = zone;
        e.startingItem1Id = i1; e.startingItem1Count = i1 ? 1 : 0;
        e.startingItem2Id = i2; e.startingItem2Count = i2 ? 1 : 0;
        e.startingItem3Id = i3; e.startingItem3Count = i3 ? 1 : 0;
        e.startingSpell1Id = s1;
        e.startingSpell2Id = s2;
        e.startingLevel = 1;
        e.iconColorRGBA = packRgba(100, 140, 240);  // alliance blue
        c.entries.push_back(e);
    };
    // Map 0 = Eastern Kingdoms, Map 1 = Kalimdor, Map 530 = Outland.
    // Coordinates are illustrative - match standard 3.3.5a starting
    // positions for each race's home zone.
    add(1, "HumanWarrior",  RACE_HUMAN,    CLS_WARRIOR,  0,    9,
        -8949.95f,  -132.49f,  83.53f, 0.0f,
        25, 38, 6948, 78, 81, "Human Warrior - Northshire Abbey, Elwynn Forest.");
    add(2, "DwarfHunter",   RACE_DWARF,    CLS_HUNTER,   0,  132,
        -6240.32f,   331.03f, 382.76f, 6.18f,
        117, 38, 6948, 75, 2480, "Dwarf Hunter - Coldridge Valley, Dun Morogh.");
    add(3, "NightElfDruid", RACE_NIGHTELF, CLS_DRUID,    1,  141,
        10311.30f,   832.97f, 1326.41f, 5.69f,
        2070, 38, 6948, 5176, 8946, "Night Elf Druid - Shadowglen, Teldrassil.");
    add(4, "GnomeMage",     RACE_GNOME,    CLS_MAGE,     0,  132,
        -6240.32f,   331.03f, 382.76f, 6.18f,
        2362, 38, 6948, 168, 5009, "Gnome Mage - Coldridge Valley, Dun Morogh.");
    add(5, "DraeneiShaman", RACE_DRAENEI,  CLS_SHAMAN,  530, 3524,
        -3961.64f, -13931.20f, 100.61f, 2.08f,
        24146, 38, 6948, 403, 332, "Draenei Shaman - Ammen Vale, Azuremyst Isle.");
    return c;
}

WoweePlayerSpawnProfile WoweePlayerSpawnProfileLoader::makeHorde(
    const std::string& catalogName) {
    using P = WoweePlayerSpawnProfile;
    WoweePlayerSpawnProfile c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t race,
                    uint32_t cls, uint32_t map, uint32_t zone,
                    float x, float y, float z, float facing,
                    uint32_t i1, uint32_t i2, uint32_t i3,
                    uint32_t s1, uint32_t s2, const char* desc) {
        P::Entry e;
        e.profileId = id; e.name = name; e.description = desc;
        e.raceMask = race; e.classMask = cls;
        e.mapId = map; e.zoneId = zone;
        e.spawnX = x; e.spawnY = y; e.spawnZ = z;
        e.spawnFacing = facing;
        e.bindMapId = map; e.bindZoneId = zone;
        e.startingItem1Id = i1; e.startingItem1Count = i1 ? 1 : 0;
        e.startingItem2Id = i2; e.startingItem2Count = i2 ? 1 : 0;
        e.startingItem3Id = i3; e.startingItem3Count = i3 ? 1 : 0;
        e.startingSpell1Id = s1;
        e.startingSpell2Id = s2;
        e.startingLevel = 1;
        e.iconColorRGBA = packRgba(220, 60, 60);    // horde red
        c.entries.push_back(e);
    };
    add(100, "OrcWarrior",     RACE_ORC,       CLS_WARRIOR, 1,  14,
        -618.51f,  -4251.67f, 38.71f, 4.74f,
        25, 38, 6948, 78, 81, "Orc Warrior - Valley of Trials, Durotar.");
    add(101, "TaurenDruid",    RACE_TAUREN,    CLS_DRUID,   1, 215,
        -2917.58f,  -257.98f, 52.99f, 5.27f,
        2070, 38, 6948, 5176, 8946, "Tauren Druid - Camp Narache, Mulgore.");
    add(102, "UndeadMage",     RACE_UNDEAD,    CLS_MAGE,    0,  85,
        1676.71f,  1677.45f, 121.67f, 2.70f,
        2362, 38, 6948, 168, 5009, "Undead Mage - Deathknell, Tirisfal Glades.");
    add(103, "TrollHunter",    RACE_TROLL,     CLS_HUNTER,  1,  14,
        -618.51f,  -4251.67f, 38.71f, 4.74f,
        117, 38, 6948, 75, 2480, "Troll Hunter - Valley of Trials, Durotar.");
    add(104, "BloodElfPriest", RACE_BLOODELF,  CLS_PRIEST, 530, 3431,
        10349.60f, -6357.29f, 33.43f, 5.31f,
        24145, 38, 6948, 585, 2050, "Blood Elf Priest - Sunstrider Isle, Eversong.");
    return c;
}

WoweePlayerSpawnProfile WoweePlayerSpawnProfileLoader::makeDeathKnight(
    const std::string& catalogName) {
    using P = WoweePlayerSpawnProfile;
    WoweePlayerSpawnProfile c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t race,
                    const char* desc) {
        P::Entry e;
        e.profileId = id; e.name = name; e.description = desc;
        e.raceMask = race;
        e.classMask = CLS_DK;
        // Acherus, the Ebon Hold - instance map 609.
        e.mapId = 609; e.zoneId = 4298;
        e.spawnX = 2406.18f;
        e.spawnY = -5342.66f;
        e.spawnZ = 382.34f;
        e.spawnFacing = 1.43f;
        e.bindMapId = 609; e.bindZoneId = 4298;
        // Standard DK starter loadout: Acherus Knight's
        // sword, mail set, runeforging knowledge.
        e.startingItem1Id = 38147;     // Tabard of the Lich King
        e.startingItem1Count = 1;
        e.startingItem2Id = 38149;     // Acherus Knight sword
        e.startingItem2Count = 1;
        // Standard DK starter spells: Death Coil, Plague
        // Strike, Death Grip.
        e.startingSpell1Id = 47541;    // Death Coil
        e.startingSpell2Id = 45462;    // Plague Strike
        e.startingSpell3Id = 49576;    // Death Grip
        e.startingLevel = 55;
        e.iconColorRGBA = packRgba(140, 240, 220);   // DK frost cyan
        c.entries.push_back(e);
    };
    add(200, "AllianceHumanDK", RACE_HUMAN,
        "Alliance Human Death Knight - starts at lvl 55 in "
        "Acherus, the Ebon Hold.");
    add(201, "HordeOrcDK",      RACE_ORC,
        "Horde Orc Death Knight - starts at lvl 55 in "
        "Acherus, the Ebon Hold.");
    return c;
}

} // namespace pipeline
} // namespace wowee
