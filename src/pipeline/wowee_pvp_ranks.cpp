#include "pipeline/wowee_pvp_ranks.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'R', 'G'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wprg";

} // namespace

const WoweePvPRanks::Entry*
WoweePvPRanks::findById(uint32_t rankId) const {
    for (const auto& e : entries)
        if (e.rankId == rankId) return &e;
    return nullptr;
}

std::vector<const WoweePvPRanks::Entry*>
WoweePvPRanks::findByFaction(uint8_t faction) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.factionFilter == faction) out.push_back(&e);
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->tier < b->tier;
              });
    return out;
}

const WoweePvPRanks::Entry*
WoweePvPRanks::findByTier(uint8_t faction, uint8_t tier) const {
    for (const auto& e : entries) {
        if (e.factionFilter == faction && e.tier == tier)
            return &e;
    }
    return nullptr;
}

bool WoweePvPRanksLoader::save(const WoweePvPRanks& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweePvPRanks::Entry& e) {
        writePOD(os, e.rankId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.factionFilter);
        writePOD(os, e.tier);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.honorRequiredWeekly);
        writePOD(os, e.honorRequiredAchieve);
        writeStr(os, e.titlePrefix);
        writePOD(os, e.gearItemId);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweePvPRanks WoweePvPRanksLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweePvPRanks>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweePvPRanks::Entry& e) {
        if (!readPOD(is, e.rankId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.factionFilter) ||
            !readPOD(is, e.tier) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.honorRequiredWeekly) ||
            !readPOD(is, e.honorRequiredAchieve)) { return false; }
        if (!readStr(is, e.titlePrefix)) { return false; }
        if (!readPOD(is, e.gearItemId) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweePvPRanksLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweePvPRanks WoweePvPRanksLoader::makeAllianceRanks(
    const std::string& catalogName) {
    using P = WoweePvPRanks;
    WoweePvPRanks c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t tier, uint32_t weekly,
                    uint32_t achieve, const char* title,
                    uint32_t gearId, const char* desc) {
        P::Entry e;
        e.rankId = id; e.name = name; e.description = desc;
        e.factionFilter = P::AllianceOnly;
        e.tier = tier;
        e.honorRequiredWeekly = weekly;
        e.honorRequiredAchieve = achieve;
        e.titlePrefix = title;
        e.gearItemId = gearId;
        e.iconColorRGBA = packRgba(140, 200, 255);   // alliance blue
        c.entries.push_back(e);
    };
    // Vanilla 1.x PvP rank thresholds (RP per week).
    // Actual values are approximate - tuned for the
    // exponential decay curve.
    add(1, "Private",         1, 200, 0, "Private",
        0,
        "Tier 1 - entry rank. Earn any honor in a "
        "week to enter and progress.");
    add(2, "Corporal",        2, 1000, 2000, "Corporal",
        0,
        "Tier 2 - first promotion. 1000 RP/week to "
        "maintain, 2000 lifetime to first-time "
        "achieve.");
    add(3, "Sergeant",        3, 2500, 5000, "Sergeant",
        0,
        "Tier 3 - squad leader. Begin gaining minor "
        "vendor discounts at this tier.");
    add(4, "MasterSergeant",  4, 5000, 12000, "Master Sergeant",
        18837,
        "Tier 4 - Master Sergeant. First gear unlock "
        "(item 18837 - Knight's Pauldrons placeholder).");
    add(5, "SergeantMajor",   5, 9000, 25000, "Sergeant Major",
        18838,
        "Tier 5 - Senior NCO rank. Tier 5 set piece "
        "unlocks.");
    add(6, "Knight",          6, 14000, 50000, "Knight",
        18839,
        "Tier 6 - first officer rank. Knighthood "
        "ceremony at Stormwind Cathedral.");
    add(7, "KnightLieutenant", 7, 22000, 100000, "Knight-Lieutenant",
        18840,
        "Tier 7 - Knight-Lieutenant. Lower-tier "
        "officer; eligible for raid-officer hall access.");
    return c;
}

WoweePvPRanks WoweePvPRanksLoader::makeHordeRanks(
    const std::string& catalogName) {
    using P = WoweePvPRanks;
    WoweePvPRanks c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t tier, uint32_t weekly,
                    uint32_t achieve, const char* title,
                    uint32_t gearId, const char* desc) {
        P::Entry e;
        e.rankId = id; e.name = name; e.description = desc;
        e.factionFilter = P::HordeOnly;
        e.tier = tier;
        e.honorRequiredWeekly = weekly;
        e.honorRequiredAchieve = achieve;
        e.titlePrefix = title;
        e.gearItemId = gearId;
        e.iconColorRGBA = packRgba(220, 80, 80);   // horde red
        c.entries.push_back(e);
    };
    // Mirrored Horde ladder - same honor thresholds,
    // distinct titles.
    add(100, "Scout",           1, 200, 0, "Scout",
        0,
        "Tier 1 - Horde entry rank. Mirrors Alliance "
        "Private.");
    add(101, "Grunt",           2, 1000, 2000, "Grunt",
        0,
        "Tier 2 - first promotion. Mirrors Corporal.");
    add(102, "HordeSergeant",   3, 2500, 5000, "Sergeant",
        0,
        "Tier 3 - squad leader. Same title as Alliance "
        "Sergeant (factionFilter disambiguates).");
    add(103, "SeniorSergeant",  4, 5000, 12000, "Senior Sergeant",
        18857,
        "Tier 4 - first gear unlock for Horde. "
        "Mirrors Master Sergeant.");
    add(104, "FirstSergeant",   5, 9000, 25000, "First Sergeant",
        18858,
        "Tier 5 - Senior Horde NCO.");
    add(105, "StoneGuard",      6, 14000, 50000, "Stone Guard",
        18859,
        "Tier 6 - first Horde officer rank. Mirrors "
        "Knight.");
    add(106, "BloodGuard",      7, 22000, 100000, "Blood Guard",
        18860,
        "Tier 7 - Lower-tier Horde officer.");
    return c;
}

WoweePvPRanks WoweePvPRanksLoader::makeHighRanks(
    const std::string& catalogName) {
    using P = WoweePvPRanks;
    WoweePvPRanks c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t faction, uint8_t tier,
                    uint32_t weekly, uint32_t achieve,
                    const char* title, uint32_t gearId,
                    uint32_t color, const char* desc) {
        P::Entry e;
        e.rankId = id; e.name = name; e.description = desc;
        e.factionFilter = faction;
        e.tier = tier;
        e.honorRequiredWeekly = weekly;
        e.honorRequiredAchieve = achieve;
        e.titlePrefix = title;
        e.gearItemId = gearId;
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    // Tiers 8-11. Tier 14 (Grand Marshal / High Warlord)
    // is intentionally omitted - it's the legendary
    // top-rank that historically required dedicated
    // 24/7 grinding of months. Catalog can be extended.
    add(200, "KnightCaptain", P::AllianceOnly, 8, 35000,
        200000, "Knight-Captain", 18841,
        packRgba(140, 200, 255),
        "Tier 8 Alliance - Knight-Captain. First high-"
        "tier rank requiring dedicated effort.");
    add(201, "KnightChampion", P::AllianceOnly, 9, 50000,
        400000, "Knight-Champion", 18842,
        packRgba(140, 200, 255),
        "Tier 9 Alliance - Knight-Champion. Mounts "
        "and legendary battlegear shoulders unlock.");
    add(202, "LtCommander", P::AllianceOnly, 10, 75000,
        650000, "Lieutenant Commander", 18843,
        packRgba(140, 200, 255),
        "Tier 10 Alliance - Lt. Commander. Unlocks "
        "the Battlemaster's Aegis hall keys.");
    add(203, "Commander", P::AllianceOnly, 11, 100000,
        1000000, "Commander", 18844,
        packRgba(140, 200, 255),
        "Tier 11 Alliance - Commander. Officer's "
        "battlegear chestpiece unlock.");
    add(210, "Legionnaire", P::HordeOnly, 8, 35000,
        200000, "Legionnaire", 18861,
        packRgba(220, 80, 80),
        "Tier 8 Horde - Legionnaire. Mirrors "
        "Knight-Captain.");
    add(211, "Centurion", P::HordeOnly, 9, 50000,
        400000, "Centurion", 18862,
        packRgba(220, 80, 80),
        "Tier 9 Horde - Centurion.");
    add(212, "Champion", P::HordeOnly, 10, 75000,
        650000, "Champion", 18863,
        packRgba(220, 80, 80),
        "Tier 10 Horde - Champion.");
    add(213, "LtCommanderHorde", P::HordeOnly, 11, 100000,
        1000000, "Lieutenant Commander", 18864,
        packRgba(220, 80, 80),
        "Tier 11 Horde - Lieutenant Commander. "
        "Unlocks the Warlord's hall keys.");
    return c;
}

} // namespace pipeline
} // namespace wowee
