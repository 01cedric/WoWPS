#include "pipeline/wowee_combat_formulas.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'F', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcfr";

} // namespace

const WoweeCombatFormulas::Entry*
WoweeCombatFormulas::findById(uint32_t formulaId) const {
    for (const auto& e : entries)
        if (e.formulaId == formulaId) return &e;
    return nullptr;
}

std::vector<const WoweeCombatFormulas::Entry*>
WoweeCombatFormulas::findApplicable(uint8_t outputStatKind,
                                       uint8_t classId,
                                       uint8_t level) const {
    std::vector<const Entry*> out;
    uint16_t classMask = static_cast<uint16_t>(1u << classId);
    for (const auto& e : entries) {
        if (e.outputStatKind != outputStatKind) continue;
        // Class filter: 0 = all classes, otherwise
        // bitmask must include this class.
        if (e.classRestriction != 0 &&
            (e.classRestriction & classMask) == 0)
            continue;
        // Level gating: levelMin must be <= level
        // and levelMax (when set) must be >= level.
        if (e.levelMin > 0 && level < e.levelMin) continue;
        if (e.levelMax > 0 && level > e.levelMax) continue;
        out.push_back(&e);
    }
    return out;
}

bool WoweeCombatFormulasLoader::save(
    const WoweeCombatFormulas& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.formulaId);
        writeStr(os, e.name);
        writePOD(os, e.outputStatKind);
        writePOD(os, e.inputStatKind);
        writePOD(os, e.levelMin);
        writePOD(os, e.levelMax);
        writePOD(os, e.classRestriction);
        writePOD(os, e.pad0);
        writePOD(os, e.conversionRatioFp_x100);
    });
}

WoweeCombatFormulas WoweeCombatFormulasLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCombatFormulas>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCombatFormulas::Entry& e) {
        if (!readPOD(is, e.formulaId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.outputStatKind) ||
            !readPOD(is, e.inputStatKind) ||
            !readPOD(is, e.levelMin) ||
            !readPOD(is, e.levelMax) ||
            !readPOD(is, e.classRestriction) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.conversionRatioFp_x100)) { return false; }
                                  return true;
                              });
}

bool WoweeCombatFormulasLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeCombatFormulas::Entry makeFormula(
    uint32_t formulaId, const char* name,
    uint8_t outputStatKind, uint8_t inputStatKind,
    uint16_t classRestriction,
    uint8_t levelMin, uint8_t levelMax,
    uint32_t conversionRatioFp_x100) {
    WoweeCombatFormulas::Entry e;
    e.formulaId = formulaId; e.name = name;
    e.outputStatKind = outputStatKind;
    e.inputStatKind = inputStatKind;
    e.classRestriction = classRestriction;
    e.levelMin = levelMin; e.levelMax = levelMax;
    e.conversionRatioFp_x100 = conversionRatioFp_x100;
    return e;
}

} // namespace

WoweeCombatFormulas WoweeCombatFormulasLoader::makeWarriorFormulas(
    const std::string& catalogName) {
    using F = WoweeCombatFormulas;
    WoweeCombatFormulas c;
    c.name = catalogName;
    constexpr uint16_t kWarrior = 1u << 1;  // class id 1
    // Warrior Str→AP: 1 Strength = 2 Attack Power.
    // Vanilla canonical ratio. Applies all levels.
    c.entries.push_back(makeFormula(
        1, "Warrior Strength to AP",
        F::AttackPower, F::Strength,
        kWarrior, 1, 60, 200 /* 2.00 */));
    // Warrior Agi→CritPct: 20 Agility = 1% crit
    // (= 0.05% per Agi). Applies all levels.
    c.entries.push_back(makeFormula(
        2, "Warrior Agility to Crit",
        F::CritPct, F::Agility,
        kWarrior, 1, 60, 5 /* 0.05% */));
    // Warrior Agi→DodgePct: 20 Agility = 1% dodge
    // (same 0.05% per Agi).
    c.entries.push_back(makeFormula(
        3, "Warrior Agility to Dodge",
        F::DodgePct, F::Agility,
        kWarrior, 1, 60, 5));
    // Warrior Sta→ no derived stat in this preset
    // (Stamina contributes to HP via WCST baseline,
    // not a derived ratio).
    c.entries.push_back(makeFormula(
        4, "Warrior Strength to ParryPct",
        F::ParryPct, F::Strength,
        kWarrior, 30, 60 /* parry from Str only at
                            level 30+ */,
        4 /* 0.04% per Str */));
    return c;
}

WoweeCombatFormulas WoweeCombatFormulasLoader::makeMageFormulas(
    const std::string& catalogName) {
    using F = WoweeCombatFormulas;
    WoweeCombatFormulas c;
    c.name = catalogName;
    constexpr uint16_t kMage = 1u << 8;  // class id 8
    // Mage Int→SpellPower: 1 Intellect = 1 SP at
    // 1.0 ratio. No level gating.
    c.entries.push_back(makeFormula(
        10, "Mage Intellect to SpellPower",
        F::SpellPower, F::Intellect,
        kMage, 1, 60, 100 /* 1.00 */));
    // Mage Int→SpellCritPct: 60 Int = 1% spell crit
    // (= 0.0167% per Int = 1.67/100 fp).
    c.entries.push_back(makeFormula(
        11, "Mage Intellect to SpellCrit",
        F::SpellCritPct, F::Intellect,
        kMage, 1, 60, 2 /* approx 0.02% per Int */));
    // Mage Spi→regen: 5 Spirit per 5sec rule covered
    // by a separate regen formula. Here just record
    // that Spi contributes 0.50 SpellPower per Spi
    // out-of-combat (placeholder for the canonical
    // regen mechanic).
    c.entries.push_back(makeFormula(
        12, "Mage Spirit to OOC SpellPower",
        F::SpellPower, F::Spirit,
        kMage, 1, 60, 50 /* 0.50 */));
    return c;
}

WoweeCombatFormulas WoweeCombatFormulasLoader::makeRogueFormulas(
    const std::string& catalogName) {
    using F = WoweeCombatFormulas;
    WoweeCombatFormulas c;
    c.name = catalogName;
    constexpr uint16_t kRogue = 1u << 4;  // class id 4
    // Rogue Str→AP: 1 Strength = 1 AP (vs Warrior's
    // 2.0 - demonstrates per-class ratio variation).
    c.entries.push_back(makeFormula(
        20, "Rogue Strength to AP",
        F::AttackPower, F::Strength,
        kRogue, 1, 60, 100 /* 1.00 */));
    // Rogue Agi→AP: 1 Agility = 1 AP (Agi-based
    // class).
    c.entries.push_back(makeFormula(
        21, "Rogue Agility to AP",
        F::AttackPower, F::Agility,
        kRogue, 1, 60, 100));
    // Rogue Agi→CritPct: 14 Agility = 1% crit
    // (= 0.0714% per Agi = ~7 in fp_x100).
    // Significantly better than Warrior 0.05.
    c.entries.push_back(makeFormula(
        22, "Rogue Agility to Crit",
        F::CritPct, F::Agility,
        kRogue, 1, 60, 7 /* 0.07% per Agi */));
    // Rogue Agi→DodgePct: 14.7 Agility = 1% dodge
    // (= 0.068% per Agi = 7 in fp_x100). Same
    // tighter ratio as crit.
    c.entries.push_back(makeFormula(
        23, "Rogue Agility to Dodge",
        F::DodgePct, F::Agility,
        kRogue, 1, 60, 7));
    return c;
}

} // namespace pipeline
} // namespace wowee
