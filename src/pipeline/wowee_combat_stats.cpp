#include "pipeline/wowee_combat_stats.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'S', 'T'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcst";

} // namespace

const WoweeCombatStats::Entry*
WoweeCombatStats::findById(uint32_t statId) const {
    for (const auto& e : entries)
        if (e.statId == statId) return &e;
    return nullptr;
}

const WoweeCombatStats::Entry*
WoweeCombatStats::find(uint8_t classId, uint8_t level) const {
    for (const auto& e : entries)
        if (e.classId == classId && e.level == level)
            return &e;
    return nullptr;
}

std::vector<const WoweeCombatStats::Entry*>
WoweeCombatStats::findByClass(uint8_t classId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.classId == classId) out.push_back(&e);
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->level < b->level;
              });
    return out;
}

bool WoweeCombatStatsLoader::save(const WoweeCombatStats& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCombatStats::Entry& e) {
        writePOD(os, e.statId);
        writePOD(os, e.classId);
        writePOD(os, e.level);
        writePOD(os, e.pad0);
        writePOD(os, e.baseHealth);
        writePOD(os, e.baseMana);
        writePOD(os, e.baseStrength);
        writePOD(os, e.baseAgility);
        writePOD(os, e.baseStamina);
        writePOD(os, e.baseIntellect);
        writePOD(os, e.baseSpirit);
        writePOD(os, e.pad1);
        writePOD(os, e.baseArmor);
                       });
}

WoweeCombatStats WoweeCombatStatsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCombatStats>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCombatStats::Entry& e) {
        if (!readPOD(is, e.statId) ||
            !readPOD(is, e.classId) ||
            !readPOD(is, e.level) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.baseHealth) ||
            !readPOD(is, e.baseMana) ||
            !readPOD(is, e.baseStrength) ||
            !readPOD(is, e.baseAgility) ||
            !readPOD(is, e.baseStamina) ||
            !readPOD(is, e.baseIntellect) ||
            !readPOD(is, e.baseSpirit) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.baseArmor)) { return false; }
                                  return true;
                              });
}

bool WoweeCombatStatsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

struct StatRow {
    uint32_t statId;
    uint8_t classId;
    uint8_t level;
    uint32_t hp;
    uint32_t mana;
    uint16_t str;
    uint16_t agi;
    uint16_t sta;
    uint16_t intel;
    uint16_t spi;
    uint32_t armor;
};

WoweeCombatStats fromRows(const std::string& catalogName,
                            const std::vector<StatRow>& rows) {
    WoweeCombatStats c;
    c.name = catalogName;
    for (const auto& r : rows) {
        WoweeCombatStats::Entry e;
        e.statId = r.statId;
        e.classId = r.classId;
        e.level = r.level;
        e.baseHealth = r.hp;
        e.baseMana = r.mana;
        e.baseStrength = r.str;
        e.baseAgility = r.agi;
        e.baseStamina = r.sta;
        e.baseIntellect = r.intel;
        e.baseSpirit = r.spi;
        e.baseArmor = r.armor;
        c.entries.push_back(e);
    }
    return c;
}

} // namespace

WoweeCombatStats WoweeCombatStatsLoader::makeWarriorStats(
    const std::string& catalogName) {
    // Warrior (classId=1) sparse sample. Numbers
    // approximate vanilla 1.12 base stats - Warrior
    // uses Rage so baseMana=0 across all levels.
    // Stats grow steadily; armor scales with Agility.
    return fromRows(catalogName, {
        {101, 1,  1,    60,    0, 23, 20, 23, 20, 20,  60},
        {102, 1, 10,   180,    0, 30, 26, 31, 22, 22, 130},
        {103, 1, 20,   400,    0, 40, 34, 42, 24, 24, 220},
        {104, 1, 30,   720,    0, 52, 44, 56, 26, 26, 330},
        {105, 1, 40,  1140,    0, 66, 56, 72, 28, 28, 460},
        {106, 1, 60,  2200,    0, 95, 80,105, 32, 32, 760},
    });
}

WoweeCombatStats WoweeCombatStatsLoader::makeMageStats(
    const std::string& catalogName) {
    // Mage (classId=8) sparse sample. baseMana grows
    // with Intellect - Mage is the canonical mana-
    // user. Lower base HP, higher Int/Spi than
    // warrior at every level.
    return fromRows(catalogName, {
        {801, 8,  1,    50,  100, 20, 20, 20, 23, 23,  40},
        {802, 8, 10,   140,  340, 22, 22, 24, 32, 30,  90},
        {803, 8, 20,   320,  720, 25, 25, 30, 44, 40, 160},
        {804, 8, 30,   580, 1180, 28, 28, 38, 58, 52, 240},
        {805, 8, 40,   920, 1740, 32, 32, 48, 74, 66, 340},
        {806, 8, 60,  1780, 3120, 40, 40, 70,108, 95, 580},
    });
}

WoweeCombatStats WoweeCombatStatsLoader::makeStartingLevels(
    const std::string& catalogName) {
    // All 9 vanilla classes at level 1. classId 6
    // (Death Knight) and 10 (Monk) are unused in
    // vanilla - skipped. Numbers reflect the per-
    // class racial-base-stat skew (Warrior/Paladin
    // high Str, Hunter/Rogue high Agi, Mage/Priest/
    // Warlock high Int, Shaman/Druid balanced).
    return fromRows(catalogName, {
        // statId class lvl   hp  mana str agi sta int spi armor
        {1001, 1,  1,    60,    0, 23, 20, 23, 20, 20,  60},
        {1002, 2,  1,    60,  100, 22, 20, 22, 20, 21,  60},  // Paladin
        {1003, 3,  1,    50,    0, 21, 23, 20, 20, 21,  50},  // Hunter
        {1004, 4,  1,    55,    0, 20, 23, 21, 20, 20,  55},  // Rogue
        {1005, 5,  1,    50,  120, 20, 20, 20, 22, 24,  40},  // Priest
        {1007, 7,  1,    55,  100, 22, 21, 22, 21, 22,  50},  // Shaman
        {1008, 8,  1,    50,  100, 20, 20, 20, 23, 23,  40},  // Mage
        {1009, 9,  1,    50,  100, 20, 20, 21, 23, 22,  40},  // Warlock
        {1011, 11, 1,    55,  100, 21, 21, 22, 22, 22,  50},  // Druid
    });
}

} // namespace pipeline
} // namespace wowee
