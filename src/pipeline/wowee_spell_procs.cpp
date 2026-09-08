#include "pipeline/wowee_spell_procs.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'P', 'S'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wsps";

} // namespace

const WoweeSpellProc::Entry*
WoweeSpellProc::findById(uint32_t procId) const {
    for (const auto& e : entries)
        if (e.procId == procId) return &e;
    return nullptr;
}

const char* WoweeSpellProc::procFlagName(uint32_t bit) {
    switch (bit) {
        case DealtMeleeAutoAttack:  return "DealtMeleeAutoAttack";
        case DealtMeleeSpell:       return "DealtMeleeSpell";
        case TakenMeleeAutoAttack:  return "TakenMeleeAutoAttack";
        case TakenMeleeSpell:       return "TakenMeleeSpell";
        case DealtRangedAutoAttack: return "DealtRangedAutoAttack";
        case DealtRangedSpell:      return "DealtRangedSpell";
        case DealtSpell:            return "DealtSpell";
        case DealtSpellHeal:        return "DealtSpellHeal";
        case TakenSpell:            return "TakenSpell";
        case OnKill:                return "OnKill";
        case OnDeath:               return "OnDeath";
        case OnCastFinished:        return "OnCastFinished";
        case Critical:              return "Critical";
        default:                    return "Unknown";
    }
}

bool WoweeSpellProcLoader::save(const WoweeSpellProc& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellProc::Entry& e) {
        writePOD(os, e.procId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.triggerSpellId);
        writePOD(os, e.procFromSpellId);
        writePOD(os, e.procChance);
        writePOD(os, e.procPpm);
        writePOD(os, e.procFlags);
        writePOD(os, e.internalCooldownMs);
        writePOD(os, e.charges);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellProc WoweeSpellProcLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellProc>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellProc::Entry& e) {
        if (!readPOD(is, e.procId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.triggerSpellId) ||
            !readPOD(is, e.procFromSpellId) ||
            !readPOD(is, e.procChance) ||
            !readPOD(is, e.procPpm) ||
            !readPOD(is, e.procFlags) ||
            !readPOD(is, e.internalCooldownMs) ||
            !readPOD(is, e.charges) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellProcLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellProc WoweeSpellProcLoader::makeWeapon(
    const std::string& catalogName) {
    using P = WoweeSpellProc;
    WoweeSpellProc c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t triggerSpell,
                    float ppm, uint32_t cd, const char* desc) {
        P::Entry e;
        e.procId = id; e.name = name; e.description = desc;
        e.triggerSpellId = triggerSpell;
        // Weapon imbue procs all key off DealtMeleeAutoAttack
        // (white swings only) with PPM-style chance.
        e.procFlags = P::DealtMeleeAutoAttack;
        e.procPpm = ppm;
        e.internalCooldownMs = cd;
        e.iconColorRGBA = packRgba(220, 180, 100);   // weapon yellow
        c.entries.push_back(e);
    };
    // Spell ids match canonical 3.3.5a weapon-imbue triggers.
    add(1, "WindfuryWeapon",   25504, 20.0f, 3000,
        "Windfury Weapon - 20 PPM extra attack with 3s ICD.");
    add(2, "FrostbrandWeapon", 8034,  9.0f,     0,
        "Frostbrand Weapon - 9 PPM frost damage + slow.");
    add(3, "FlametongueWeapon",10444, 15.0f,    0,
        "Flametongue Weapon - 15 PPM fire damage on hit.");
    add(4, "ManaOilTorch",     28568, 4.0f,  5000,
        "Brilliant Mana Oil - 4 PPM mana restore on hit, "
        "5s ICD.");
    return c;
}

WoweeSpellProc WoweeSpellProcLoader::makeAura(
    const std::string& catalogName) {
    using P = WoweeSpellProc;
    WoweeSpellProc c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t triggerSpell,
                    uint32_t flags, float chance, uint32_t cd,
                    const char* desc) {
        P::Entry e;
        e.procId = id; e.name = name; e.description = desc;
        e.triggerSpellId = triggerSpell;
        e.procFlags = flags;
        e.procChance = chance;
        e.internalCooldownMs = cd;
        e.iconColorRGBA = packRgba(220, 200, 100);   // holy gold
        c.entries.push_back(e);
    };
    // Aura-tied procs - buff is on the player, fires when
    // they take/deal qualifying actions.
    add(100, "BlessingOfWisdomMana", 27144,
        P::DealtMeleeAutoAttack | P::DealtMeleeSpell,
        1.0f, 0,
        "Blessing of Wisdom - 100%% mana return on melee/spell.");
    add(101, "MoltenArmorCrit",       30482,
        P::TakenMeleeAutoAttack | P::TakenMeleeSpell,
        0.05f, 0,
        "Molten Armor - 5%% damage reflect on incoming melee.");
    add(102, "EarthShieldHeal",       974,
        P::TakenSpell | P::TakenMeleeSpell,
        1.0f, 1500,
        "Earth Shield - 100%% heal on damage taken, 1.5s ICD.");
    add(103, "JudgementOfWisdom",     20186,
        P::DealtMeleeAutoAttack | P::DealtMeleeSpell,
        0.5f, 0,
        "Judgement of Wisdom - 50%% mana return per hit on "
        "judged target.");
    return c;
}

WoweeSpellProc WoweeSpellProcLoader::makeTalent(
    const std::string& catalogName) {
    using P = WoweeSpellProc;
    WoweeSpellProc c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t triggerSpell,
                    uint32_t fromSpell, uint32_t flags, float chance,
                    uint32_t cd, uint8_t charges, const char* desc) {
        P::Entry e;
        e.procId = id; e.name = name; e.description = desc;
        e.triggerSpellId = triggerSpell;
        e.procFromSpellId = fromSpell;
        e.procFlags = flags;
        e.procChance = chance;
        e.internalCooldownMs = cd;
        e.charges = charges;
        e.iconColorRGBA = packRgba(180, 100, 240);   // talent purple
        c.entries.push_back(e);
    };
    add(200, "Clearcasting",       12536, 0,
        P::DealtSpell, 0.10f, 0, 1,
        "Mage Arcane Concentration - 10%% chance per cast for "
        "next-spell free, 1 charge.");
    add(201, "OmenOfClarity",      16870, 0,
        P::DealtMeleeAutoAttack | P::DealtSpell, 0.06f, 0, 1,
        "Druid Omen of Clarity - 6%% per swing/cast for "
        "next-ability free, 1 charge.");
    add(202, "SealOfRighteousness",25742, 21084,
        P::DealtMeleeAutoAttack, 1.0f, 0, 0,
        "Paladin Seal of Righteousness - 100%% on melee swing "
        "while seal active.");
    add(203, "Nightfall",          17941, 0,
        P::DealtSpell, 0.04f, 0, 1,
        "Warlock Nightfall - 4%% per Drain Soul/Corruption tick "
        "for instant Shadow Bolt, 1 charge.");
    return c;
}

} // namespace pipeline
} // namespace wowee
