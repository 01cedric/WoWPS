#include "pipeline/wowee_stat_curves.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'T', 'M'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wstm";

} // namespace

const WoweeStatCurve::Entry*
WoweeStatCurve::findById(uint32_t curveId) const {
    for (const auto& e : entries)
        if (e.curveId == curveId) return &e;
    return nullptr;
}

float WoweeStatCurve::resolveAtLevel(uint32_t curveId,
                                      uint8_t level) const {
    const Entry* e = findById(curveId);
    if (!e) return 0.0f;
    if (level < e->minLevel) return 0.0f;
    uint8_t clampedLevel = level;
    if (clampedLevel > e->maxLevel) clampedLevel = e->maxLevel;
    float v = e->baseValue +
              e->perLevelDelta * static_cast<float>(clampedLevel - 1);
    return v * e->multiplier;
}

const char* WoweeStatCurve::curveKindName(uint8_t k) {
    switch (k) {
        case Crit:       return "crit";
        case Hit:        return "hit";
        case Power:      return "power";
        case Regen:      return "regen";
        case Resist:     return "resist";
        case Mitigation: return "mitigation";
        case Misc:       return "misc";
        default:         return "unknown";
    }
}

bool WoweeStatCurveLoader::save(const WoweeStatCurve& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeStatCurve::Entry& e) {
        writePOD(os, e.curveId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.curveKind);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.pad0);
        writePOD(os, e.baseValue);
        writePOD(os, e.perLevelDelta);
        writePOD(os, e.multiplier);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeStatCurve WoweeStatCurveLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeStatCurve>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeStatCurve::Entry& e) {
        if (!readPOD(is, e.curveId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.curveKind) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.baseValue) ||
            !readPOD(is, e.perLevelDelta) ||
            !readPOD(is, e.multiplier) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeStatCurveLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeStatCurve WoweeStatCurveLoader::makeCrit(
    const std::string& catalogName) {
    using S = WoweeStatCurve;
    WoweeStatCurve c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float base,
                    float perLvl, const char* desc) {
        S::Entry e;
        e.curveId = id; e.name = name; e.description = desc;
        e.curveKind = S::Crit;
        e.baseValue = base;
        e.perLevelDelta = perLvl;
        e.iconColorRGBA = packRgba(240, 100, 100);   // crit red
        c.entries.push_back(e);
    };
    // Canonical 3.3.5a crit-chance scaling: base 5%
    // for melee/ranged at lvl 1, +0.05% per level.
    // Spell crit base 1%, +0.04% per level (mages
    // get class bonus on top).
    add(1, "MeleeCritChance",   5.0f,  0.05f,
        "Melee crit chance - base 5%% at lvl 1, +0.05%% per level.");
    add(2, "RangedCritChance",  5.0f,  0.05f,
        "Ranged crit chance - same scaling as melee.");
    add(3, "SpellCritChance",   1.0f,  0.04f,
        "Spell crit chance - base 1%%, +0.04%% per level. "
        "Class talents add fixed bonuses.");
    add(4, "ParryChance",       5.0f,  0.0f,
        "Parry chance - flat 5%% from level 1, scales via "
        "Strength/Parry rating (see WCRR).");
    add(5, "DodgeChance",       5.0f,  0.04f,
        "Base dodge - 5%% + 0.04%%/level + Agility scaling.");
    return c;
}

WoweeStatCurve WoweeStatCurveLoader::makeRegen(
    const std::string& catalogName) {
    using S = WoweeStatCurve;
    WoweeStatCurve c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float base,
                    float perLvl, float mult, const char* desc) {
        S::Entry e;
        e.curveId = id; e.name = name; e.description = desc;
        e.curveKind = S::Regen;
        e.baseValue = base;
        e.perLevelDelta = perLvl;
        e.multiplier = mult;
        e.iconColorRGBA = packRgba(100, 200, 240);   // regen blue
        c.entries.push_back(e);
    };
    add(100, "ManaPerSpirit",    0.0f, 0.0125f, 1.0f,
        "Mana regen per Spirit out-of-combat - 0.0125 mp5/spirit "
        "scaling per level.");
    add(101, "HpPerSpirit",      0.0f, 0.05f,   1.0f,
        "Health regen per Spirit out-of-combat - 0.05 hp/sec "
        "per spirit scaling per level.");
    add(102, "EnergyPerSec",    20.0f, 0.0f,    1.0f,
        "Energy regen - flat 20 per 2s baseline (Rogue / Cat "
        "Druid). Haste reduces tick interval.");
    add(103, "RageDecayPerSec",  3.0f, 0.0f,    1.0f,
        "Rage decay out-of-combat - 3 rage per second uniformly. "
        "In-combat rage doesn't decay.");
    return c;
}

WoweeStatCurve WoweeStatCurveLoader::makeArmor(
    const std::string& catalogName) {
    using S = WoweeStatCurve;
    WoweeStatCurve c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    float base, float perLvl, const char* desc) {
        S::Entry e;
        e.curveId = id; e.name = name; e.description = desc;
        e.curveKind = kind;
        e.baseValue = base;
        e.perLevelDelta = perLvl;
        e.iconColorRGBA = packRgba(180, 180, 200);   // armor grey
        c.entries.push_back(e);
    };
    add(200, "BaseArmorPerLevel", S::Mitigation,    0.0f,  10.0f,
        "Base armor scaling - 10 armor per character level "
        "for cloth/leather wearers without items.");
    add(201, "ArmorMitigationPct", S::Mitigation,   0.0f,   0.4f,
        "Armor → damage reduction conversion - ~0.4%% per level "
        "of effectiveness against same-level attackers.");
    add(202, "ResistancePerLevel", S::Resist,       0.0f,   1.0f,
        "Magic resistance scaling - 1 resist per level for "
        "Holy / Fire / Frost / etc; capped at level*5.");
    return c;
}

} // namespace pipeline
} // namespace wowee
