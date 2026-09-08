#include "pipeline/wowee_spell_aura_types.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'U', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".waur";

} // namespace

const WoweeSpellAuraType::Entry*
WoweeSpellAuraType::findById(uint32_t auraTypeId) const {
    for (const auto& e : entries)
        if (e.auraTypeId == auraTypeId) return &e;
    return nullptr;
}

const char* WoweeSpellAuraType::auraKindName(uint8_t k) {
    switch (k) {
        case Periodic:  return "periodic";
        case StatMod:   return "stat-mod";
        case DamageMod: return "damage-mod";
        case Movement:  return "movement";
        case Visual:    return "visual";
        case Trigger:   return "trigger";
        case Resource:  return "resource";
        case Control:   return "control";
        case Misc:      return "misc";
        default:        return "unknown";
    }
}

const char* WoweeSpellAuraType::targetingHintName(uint8_t t) {
    switch (t) {
        case AnyUnit:        return "any";
        case SelfOnly:       return "self";
        case HostileOnly:    return "hostile";
        case BeneficialOnly: return "beneficial";
        default:             return "unknown";
    }
}

bool WoweeSpellAuraTypeLoader::save(const WoweeSpellAuraType& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellAuraType::Entry& e) {
        writePOD(os, e.auraTypeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.auraKind);
        writePOD(os, e.targetingHint);
        writePOD(os, e.isStackable);
        writePOD(os, e.maxStackCount);
        writePOD(os, e.updateFrequencyMs);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellAuraType WoweeSpellAuraTypeLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellAuraType>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellAuraType::Entry& e) {
        if (!readPOD(is, e.auraTypeId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.auraKind) ||
            !readPOD(is, e.targetingHint) ||
            !readPOD(is, e.isStackable) ||
            !readPOD(is, e.maxStackCount) ||
            !readPOD(is, e.updateFrequencyMs) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellAuraTypeLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellAuraType WoweeSpellAuraTypeLoader::makePeriodic(
    const std::string& catalogName) {
    using A = WoweeSpellAuraType;
    WoweeSpellAuraType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t targeting,
                    uint32_t freqMs, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        A::Entry e;
        e.auraTypeId = id; e.name = name; e.description = desc;
        e.auraKind = A::Periodic;
        e.targetingHint = targeting;
        e.updateFrequencyMs = freqMs;
        e.isStackable = 1;       // most DoTs/HoTs are stackable
        e.maxStackCount = 5;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    // Standard WotLK periodic aura type IDs from
    // SpellEffect.EffectAuraType.
    add(3,   "PeriodicDamage",       A::HostileOnly,    3000,
        220,  80,  80, "Tick damage every 3s (DoT - Corruption / Moonfire / etc).");
    add(8,   "PeriodicHeal",         A::BeneficialOnly, 3000,
         80, 240,  80, "Tick heal every 3s (HoT - Renew / Rejuvenation).");
    add(21,  "PeriodicEnergize",     A::AnyUnit,        2000,
        100, 200, 240, "Tick power restore every 2s (Innervate, Drink).");
    add(53,  "PeriodicLeech",        A::HostileOnly,    3000,
        140,  60, 200, "Tick damage every 3s, return % as caster heal "
        "(Drain Life, Death Coil-style).");
    add(23,  "PeriodicTriggerSpell", A::AnyUnit,        1000,
        220, 200, 100, "Tick triggers another spell every N ms (Mind "
        "Flay -> Shadow Damage proc).");
    return c;
}

WoweeSpellAuraType WoweeSpellAuraTypeLoader::makeStatMod(
    const std::string& catalogName) {
    using A = WoweeSpellAuraType;
    WoweeSpellAuraType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t targeting,
                    const char* desc) {
        A::Entry e;
        e.auraTypeId = id; e.name = name; e.description = desc;
        e.auraKind = A::StatMod;
        e.targetingHint = targeting;
        // Stat mods don't tick - instant on-apply.
        e.updateFrequencyMs = 0;
        e.iconColorRGBA = packRgba(180, 180, 240);   // stat blue
        c.entries.push_back(e);
    };
    add(29,  "ModStat",          A::BeneficialOnly,
        "Modify a primary stat by a flat amount (Mark of the "
        "Wild adds Stamina / Strength / etc).");
    add(22,  "ModResistance",    A::BeneficialOnly,
        "Modify school resistance (+ schoolMask, Shadow "
        "Protection grants Shadow resist).");
    add(79,  "ModDamageDone",    A::BeneficialOnly,
        "Modify spell-power-equivalent damage done by school.");
    add(170, "ModHaste",         A::BeneficialOnly,
        "Modify melee attack speed and spell cast haste "
        "(Heroism / Bloodlust / Power Infusion).");
    add(57,  "ModCritPercent",   A::BeneficialOnly,
        "Modify crit-chance percentage (Leader of the Pack).");
    return c;
}

WoweeSpellAuraType WoweeSpellAuraTypeLoader::makeMovement(
    const std::string& catalogName) {
    using A = WoweeSpellAuraType;
    WoweeSpellAuraType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* desc) {
        A::Entry e;
        e.auraTypeId = id; e.name = name; e.description = desc;
        e.auraKind = A::Movement;
        e.targetingHint = A::HostileOnly;
        e.isStackable = 0;       // movement debuffs typically don't stack
        e.iconColorRGBA = packRgba(240, 200, 100);   // CC yellow
        c.entries.push_back(e);
    };
    add(12,  "Stun",              "Stun - target cannot act or move.");
    add(33,  "ModDecreaseSpeed",  "Snare - reduce movement speed by %.");
    add(80,  "ModConfuse",        "Confuse - target wanders randomly, "
        "cannot use abilities.");
    add(83,  "Root",              "Root - target cannot move (can still act).");
    return c;
}

} // namespace pipeline
} // namespace wowee
