#include "pipeline/wowee_instance_lockouts.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'H', 'L', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".whld";

} // namespace

const WoweeInstanceLockout::Entry*
WoweeInstanceLockout::findById(uint32_t lockoutId) const {
    for (const auto& e : entries)
        if (e.lockoutId == lockoutId) return &e;
    return nullptr;
}

std::vector<const WoweeInstanceLockout::Entry*>
WoweeInstanceLockout::findByMap(uint32_t mapId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.mapId == mapId) out.push_back(&e);
    return out;
}

uint64_t WoweeInstanceLockout::nextResetMs(uint32_t lockoutId,
                                            uint64_t currentMs) const {
    const Entry* e = findById(lockoutId);
    if (!e || e->resetIntervalMs == 0) return currentMs;
    // Round up to the next interval boundary. The engine
    // overrides the epoch with its configured server reset
    // time (typically Tuesday 8:00am server-local), but the
    // catalog provides the interval shape.
    uint64_t intervals = (currentMs / e->resetIntervalMs) + 1;
    return intervals * e->resetIntervalMs;
}

const char* WoweeInstanceLockout::lockoutKindName(uint8_t k) {
    switch (k) {
        case Daily:      return "daily";
        case Weekly:     return "weekly";
        case SemiWeekly: return "semi-weekly";
        case Custom:     return "custom";
        default:         return "unknown";
    }
}

bool WoweeInstanceLockoutLoader::save(const WoweeInstanceLockout& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeInstanceLockout::Entry& e) {
        writePOD(os, e.lockoutId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.difficultyId);
        writePOD(os, e.resetIntervalMs);
        writePOD(os, e.maxBossKillsPerLockout);
        writePOD(os, e.bonusRolls);
        writePOD(os, e.raidLockoutKind);
        writePOD(os, e.raidGroupSize);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeInstanceLockout WoweeInstanceLockoutLoader::load(
    const std::string& basePath) {
    WoweeInstanceLockout out;
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    if (!is) return out;
    uint32_t entryCount = 0;
    if (!readCatalogHeader(is, kMagic, kVersion, out.name, entryCount)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.lockoutId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.difficultyId) ||
            !readPOD(is, e.resetIntervalMs) ||
            !readPOD(is, e.maxBossKillsPerLockout) ||
            !readPOD(is, e.bonusRolls) ||
            !readPOD(is, e.raidLockoutKind) ||
            !readPOD(is, e.raidGroupSize) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeInstanceLockoutLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeInstanceLockout WoweeInstanceLockoutLoader::makeRaidWeekly(
    const std::string& catalogName) {
    using L = WoweeInstanceLockout;
    WoweeInstanceLockout c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t diff,
                    uint8_t size, const char* desc) {
        L::Entry e;
        e.lockoutId = id; e.name = name; e.description = desc;
        // mapId 631 = Icecrown Citadel.
        e.mapId = 631;
        e.difficultyId = diff;
        e.resetIntervalMs = L::kWeeklyMs;
        e.maxBossKillsPerLockout = 12;     // 12 bosses in ICC
        e.raidLockoutKind = L::Weekly;
        e.raidGroupSize = size;
        e.iconColorRGBA = packRgba(220, 80, 100);    // raid red
        c.entries.push_back(e);
    };
    add(1, "ICC10Normal",  100, 10, "ICC 10-Normal weekly lockout - 12 bosses, 10 players.");
    add(2, "ICC25Normal",  101, 25, "ICC 25-Normal weekly lockout - 12 bosses, 25 players.");
    add(3, "ICC10Heroic",  102, 10, "ICC 10-Heroic weekly lockout - 12 bosses, 10 players, heroic loot tier.");
    add(4, "ICC25Heroic",  103, 25, "ICC 25-Heroic weekly lockout - 12 bosses, 25 players, heroic loot tier.");
    return c;
}

WoweeInstanceLockout WoweeInstanceLockoutLoader::makeDungeonDaily(
    const std::string& catalogName) {
    using L = WoweeInstanceLockout;
    WoweeInstanceLockout c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint8_t bosses, const char* desc) {
        L::Entry e;
        e.lockoutId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = 1;     // 5-man heroic
        e.resetIntervalMs = L::kDailyMs;
        e.maxBossKillsPerLockout = bosses;
        e.raidLockoutKind = L::Daily;
        e.raidGroupSize = 5;
        e.iconColorRGBA = packRgba(180, 220, 100);   // dungeon green
        c.entries.push_back(e);
    };
    // WotLK 5-man heroic dungeon mapIds.
    add(100, "HallsOfReflectionH",  668, 3,
        "Halls of Reflection heroic - daily lockout, 3 bosses, 5 players.");
    add(101, "ForgeOfSoulsH",       632, 2,
        "Forge of Souls heroic - daily lockout, 2 bosses, 5 players.");
    add(102, "PitOfSaronH",         658, 3,
        "Pit of Saron heroic - daily lockout, 3 bosses, 5 players.");
    add(103, "TrialOfTheChampionH", 650, 4,
        "Trial of the Champion heroic - daily lockout, 4 bosses, 5 players.");
    return c;
}

WoweeInstanceLockout WoweeInstanceLockoutLoader::makeWorldEvent(
    const std::string& catalogName) {
    using L = WoweeInstanceLockout;
    WoweeInstanceLockout c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint32_t intervalMs, uint8_t kind, uint8_t bosses,
                    const char* desc) {
        L::Entry e;
        e.lockoutId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.resetIntervalMs = intervalMs;
        e.raidLockoutKind = kind;
        e.maxBossKillsPerLockout = bosses;
        e.raidGroupSize = 5;
        e.iconColorRGBA = packRgba(240, 200, 100);   // event gold
        c.entries.push_back(e);
    };
    // World-event lockouts with non-standard intervals.
    // Wintergrasp's 2.5h cycle is the canonical Custom kind.
    add(200, "BrewfestRamDaily",     0, L::kDailyMs, L::Daily, 1,
        "Brewfest Ram Racing - daily reset, 1 reward per day.");
    add(201, "HallowsEndPumpkin",    0, L::kDailyMs, L::Daily, 1,
        "Hallow's End pumpkin spawn - daily reset, 1 candy bag per day.");
    add(202, "WintergraspBattle",  571, 9000000u, L::Custom, 1,
        "Wintergrasp battle - 2.5h reset (9000000ms) outdoor PvP zone.");
    return c;
}

} // namespace pipeline
} // namespace wowee
