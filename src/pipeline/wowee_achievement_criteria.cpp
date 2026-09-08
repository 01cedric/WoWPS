#include "pipeline/wowee_achievement_criteria.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'C', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wacr";

} // namespace

const WoweeAchievementCriteria::Entry*
WoweeAchievementCriteria::findById(uint32_t criteriaId) const {
    for (const auto& e : entries)
        if (e.criteriaId == criteriaId) return &e;
    return nullptr;
}

std::vector<const WoweeAchievementCriteria::Entry*>
WoweeAchievementCriteria::findByAchievement(
    uint32_t achievementId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.achievementId == achievementId) out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->progressOrder < b->progressOrder;
              });
    return out;
}

const char* WoweeAchievementCriteria::criteriaTypeName(uint8_t k) {
    switch (k) {
        case KillCreature:    return "kill-creature";
        case ReachLevel:      return "reach-level";
        case CompleteQuest:   return "complete-quest";
        case EarnGold:        return "earn-gold";
        case GainHonor:       return "gain-honor";
        case EarnReputation:  return "earn-reputation";
        case ExploreZone:     return "explore-zone";
        case LootItem:        return "loot-item";
        case UseItem:         return "use-item";
        case CastSpell:       return "cast-spell";
        case PvPKill:         return "pvp-kill";
        case DungeonRun:      return "dungeon-run";
        case Misc:            return "misc";
        default:              return "unknown";
    }
}

bool WoweeAchievementCriteriaLoader::save(
    const WoweeAchievementCriteria& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.criteriaId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.achievementId);
        writePOD(os, e.targetId);
        writePOD(os, e.requiredCount);
        writePOD(os, e.timeLimitMs);
        writePOD(os, e.criteriaType);
        writePOD(os, e.progressOrder);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.iconColorRGBA);
    });
}

WoweeAchievementCriteria WoweeAchievementCriteriaLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeAchievementCriteria>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeAchievementCriteria::Entry& e) {
        if (!readPOD(is, e.criteriaId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.achievementId) ||
            !readPOD(is, e.targetId) ||
            !readPOD(is, e.requiredCount) ||
            !readPOD(is, e.timeLimitMs) ||
            !readPOD(is, e.criteriaType) ||
            !readPOD(is, e.progressOrder) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeAchievementCriteriaLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeAchievementCriteria WoweeAchievementCriteriaLoader::makeKill(
    const std::string& catalogName) {
    using A = WoweeAchievementCriteria;
    WoweeAchievementCriteria c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t ach,
                    uint32_t creatureId, uint32_t count,
                    uint8_t order, const char* desc) {
        A::Entry e;
        e.criteriaId = id; e.name = name; e.description = desc;
        e.achievementId = ach;
        e.targetId = creatureId;
        e.requiredCount = count;
        e.criteriaType = A::KillCreature;
        e.progressOrder = order;
        e.iconColorRGBA = packRgba(220, 80, 80);   // kill red
        c.entries.push_back(e);
    };
    // Five kill criteria all under one composite
    // achievement (achievementId 5000) - slay diverse
    // enemies for "Kill 'Em All".
    add(1, "DefiasKills",     5000,  448, 50, 0,
        "Slay 50 Defias bandits in Westfall.");
    add(2, "MurlocKills",     5000,  346, 25, 1,
        "Slay 25 murlocs anywhere.");
    add(3, "NagaKills",       5000, 4356, 100, 2,
        "Slay 100 naga in Azshara or Maraudon.");
    add(4, "DragonKills",     5000, 6109,  1, 3,
        "Slay 1 dragon (Onyxia / Nefarian / Ysondre / etc).");
    add(5, "RareEliteKills",  5000, 7846,  1, 4,
        "Slay 1 rare elite mob (silver dragon nameplate).");
    return c;
}

WoweeAchievementCriteria WoweeAchievementCriteriaLoader::makeQuest(
    const std::string& catalogName) {
    using A = WoweeAchievementCriteria;
    WoweeAchievementCriteria c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t ach,
                    uint32_t questId, uint8_t order,
                    const char* desc) {
        A::Entry e;
        e.criteriaId = id; e.name = name; e.description = desc;
        e.achievementId = ach;
        e.targetId = questId;
        e.requiredCount = 1;
        e.criteriaType = A::CompleteQuest;
        e.progressOrder = order;
        e.iconColorRGBA = packRgba(220, 200, 100);   // quest gold
        c.entries.push_back(e);
    };
    // 4-step quest progression under achievement 5100.
    add(100, "FinishTutorial",    5100,    1, 0,
        "Complete the starting-area tutorial chain.");
    add(101, "FinishStartingZone", 5100,   24, 1,
        "Complete every quest in the level-1 starting zone.");
    add(102, "FinishDaily",        5100, 12013, 2,
        "Complete a daily quest.");
    add(103, "FinishEscort",       5100,   123, 3,
        "Complete an escort quest of any kind.");
    return c;
}

WoweeAchievementCriteria WoweeAchievementCriteriaLoader::makeMixed(
    const std::string& catalogName) {
    using A = WoweeAchievementCriteria;
    WoweeAchievementCriteria c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t ach,
                    uint8_t kind, uint32_t target, uint32_t count,
                    uint8_t order,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        A::Entry e;
        e.criteriaId = id; e.name = name; e.description = desc;
        e.achievementId = ach;
        e.targetId = target;
        e.requiredCount = count;
        e.criteriaType = kind;
        e.progressOrder = order;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    // Five different criteria types under achievement 5200
    // - demonstrate the full CriteriaType variety.
    add(200, "ReachLevel80",   5200, A::ReachLevel,    0,    80, 0,
        100, 240, 100, "Reach level 80.");
    add(201, "EarnGold10k",    5200, A::EarnGold,      0, 100000000, 1,
        220, 200, 100, "Accumulate 10000 gold (100M copper).");
    add(202, "GainHonor5k",    5200, A::GainHonor,     0,   5000, 2,
        220,  80,  80, "Earn 5000 honor points.");
    add(203, "PvPKill100",     5200, A::PvPKill,       0,    100, 3,
        180, 100, 240, "Kill 100 enemy players in PvP.");
    add(204, "ExploreStormwind",5200,A::ExploreZone, 1519,     1, 4,
        100, 140, 240, "Discover every subzone in Stormwind.");
    return c;
}

} // namespace pipeline
} // namespace wowee
