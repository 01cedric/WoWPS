#include "pipeline/wowee_loot_modes.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'M', 'A'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wlma";

} // namespace

const WoweeLootModes::Entry*
WoweeLootModes::findById(uint32_t modeId) const {
    for (const auto& e : entries)
        if (e.modeId == modeId) return &e;
    return nullptr;
}

std::vector<const WoweeLootModes::Entry*>
WoweeLootModes::findByKind(uint8_t modeKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.modeKind == modeKind) out.push_back(&e);
    return out;
}

bool WoweeLootModesLoader::save(const WoweeLootModes& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeLootModes::Entry& e) {
        writePOD(os, e.modeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.modeKind);
        writePOD(os, e.thresholdQuality);
        writePOD(os, e.masterLooterRequired);
        writePOD(os, e.idleSkipSec);
        writePOD(os, e.timeoutFallbackKind);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeLootModes WoweeLootModesLoader::load(
    const std::string& basePath) {
    WoweeLootModes out;
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    if (!is) return out;
    uint32_t entryCount = 0;
    if (!readCatalogHeader(is, kMagic, kVersion, out.name, entryCount)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.modeId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.modeKind) ||
            !readPOD(is, e.thresholdQuality) ||
            !readPOD(is, e.masterLooterRequired) ||
            !readPOD(is, e.idleSkipSec) ||
            !readPOD(is, e.timeoutFallbackKind) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeLootModesLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeLootModes WoweeLootModesLoader::makeStandard(
    const std::string& catalogName) {
    using L = WoweeLootModes;
    WoweeLootModes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t threshold, uint8_t mlReq,
                    uint8_t idleSkip, const char* desc) {
        L::Entry e;
        e.modeId = id; e.name = name; e.description = desc;
        e.modeKind = kind;
        e.thresholdQuality = threshold;
        e.masterLooterRequired = mlReq;
        e.idleSkipSec = idleSkip;
        e.timeoutFallbackKind = L::FreeForAll;
        e.iconColorRGBA = packRgba(180, 220, 100);   // standard green
        c.entries.push_back(e);
    };
    add(1, "FFAFarming", L::FreeForAll, 0, 0, 0,
        "Free-For-All - first-click wins. Common in "
        "solo/duo farming runs where all loot is "
        "destined for the AH.");
    add(2, "RoundRobinTrash", L::RoundRobin, 0, 0, 0,
        "Round-Robin - rotates through party slots, "
        "assigns loot to the next player in sequence. "
        "Standard 5-man trash policy.");
    add(3, "NeedBeforeGreedUncommon",
        L::NeedBeforeGreed, 2, 0, 0,
        "Need-Before-Greed at Uncommon threshold - "
        "anything Uncommon (green) or above triggers "
        "the Need/Greed/Pass roll dialog. Standard "
        "5-man heroic policy.");
    add(4, "MasterLootRare", L::MasterLoot, 3, 1, 0,
        "Master Loot at Rare threshold - anything "
        "Rare (blue) or above goes to the master "
        "looter for hand distribution. Master looter "
        "REQUIRED.");
    return c;
}

WoweeLootModes WoweeLootModesLoader::makeRaidPolicies(
    const std::string& catalogName) {
    using L = WoweeLootModes;
    WoweeLootModes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t threshold, uint8_t mlReq,
                    uint8_t fallback, const char* desc) {
        L::Entry e;
        e.modeId = id; e.name = name; e.description = desc;
        e.modeKind = kind;
        e.thresholdQuality = threshold;
        e.masterLooterRequired = mlReq;
        e.idleSkipSec = 0;
        e.timeoutFallbackKind = fallback;
        e.iconColorRGBA = packRgba(220, 80, 100);   // raid red
        c.entries.push_back(e);
    };
    add(100, "MasterLootEpicRaid", L::MasterLoot, 4, 1,
        L::NeedBeforeGreed,
        "Master Loot at Epic threshold - standard 25-"
        "man raid policy. Master looter REQUIRED. "
        "Fallback to Need-Before-Greed if master looter "
        "disconnects mid-distribution.");
    add(101, "PersonalLootEpic", L::Personal, 4, 0,
        L::Personal,
        "Personal Loot at Epic threshold - each player "
        "gets their own roll, no master-looter needed. "
        "Anti-drama policy for pug raids.");
    add(102, "NBGRareDefault", L::NeedBeforeGreed, 3, 0,
        L::FreeForAll,
        "Need-Before-Greed at Rare (Blue) threshold - "
        "less restrictive than Epic gating. Common "
        "10-man raid default.");
    return c;
}

WoweeLootModes WoweeLootModesLoader::makeAFKPrevention(
    const std::string& catalogName) {
    using L = WoweeLootModes;
    WoweeLootModes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t threshold, uint8_t mlReq,
                    uint8_t idleSkip, uint8_t fallback,
                    const char* desc) {
        L::Entry e;
        e.modeId = id; e.name = name; e.description = desc;
        e.modeKind = kind;
        e.thresholdQuality = threshold;
        e.masterLooterRequired = mlReq;
        e.idleSkipSec = idleSkip;
        e.timeoutFallbackKind = fallback;
        e.iconColorRGBA = packRgba(220, 200, 80);   // AFK warning yellow
        c.entries.push_back(e);
    };
    add(200, "RoundRobinIdleSkip30",
        L::RoundRobin, 0, 0, 30, L::FreeForAll,
        "Round-Robin with 30-second idle-skip - "
        "advances rotation if current pick is AFK > "
        "30s. Prevents stuck loot rotations in casual "
        "groups.");
    add(201, "MasterLootTimeout",
        L::MasterLoot, 4, 1, 0, L::NeedBeforeGreed,
        "Master Loot with timeout fallback - if master "
        "looter is unresponsive for 60s, the loot mode "
        "auto-promotes to Need-Before-Greed. Server "
        "enforces the 60s window externally.");
    add(202, "PersonalIdleSkip45",
        L::Personal, 3, 0, 45, L::Personal,
        "Personal Loot with 45-second idle-skip - "
        "each player has 45s to pick up their personal "
        "drop, then it bag-skips. Anti-AFK-leech "
        "policy.");
    return c;
}

} // namespace pipeline
} // namespace wowee
