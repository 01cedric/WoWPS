#include "pipeline/wowee_spell_effect_types.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'E', 'F'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wsef";

} // namespace

const WoweeSpellEffectType::Entry*
WoweeSpellEffectType::findById(uint32_t effectId) const {
    for (const auto& e : entries)
        if (e.effectId == effectId) return &e;
    return nullptr;
}

const char* WoweeSpellEffectType::effectKindName(uint8_t k) {
    switch (k) {
        case Damage:   return "damage";
        case Heal:     return "heal";
        case Aura:     return "aura";
        case Energize: return "energize";
        case Trigger:  return "trigger";
        case Movement: return "movement";
        case Summon:   return "summon";
        case Dispel:   return "dispel";
        case Dummy:    return "dummy";
        case Misc:     return "misc";
        default:       return "unknown";
    }
}

bool WoweeSpellEffectTypeLoader::save(const WoweeSpellEffectType& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellEffectType::Entry& e) {
        writePOD(os, e.effectId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.effectKind);
        writePOD(os, e.behaviorFlags);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.baseAmount);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellEffectType WoweeSpellEffectTypeLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellEffectType>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellEffectType::Entry& e) {
        if (!readPOD(is, e.effectId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.effectKind) ||
            !readPOD(is, e.behaviorFlags) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.baseAmount) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellEffectTypeLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellEffectType WoweeSpellEffectTypeLoader::makeDamage(
    const std::string& catalogName) {
    using S = WoweeSpellEffectType;
    WoweeSpellEffectType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t flags, int32_t base, const char* desc) {
        S::Entry e;
        e.effectId = id; e.name = name; e.description = desc;
        e.effectKind = kind;
        e.behaviorFlags = flags;
        e.baseAmount = base;
        e.iconColorRGBA = packRgba(220, 80, 80);   // damage red
        c.entries.push_back(e);
    };
    // Standard WotLK damage effect IDs from Spell.dbc.
    add(2,   "SchoolDamage",         S::Damage,
        S::RequiresTarget | S::RequiresLineOfSight |
        S::IsHostileEffect | S::TriggersGCD,
        0, "School-typed magical damage (Fire / Frost / etc).");
    add(13,  "EnvironmentalDamage",  S::Damage,
        S::IsHostileEffect, 0,
        "Environmental damage (lava / falling / drowning).");
    add(58,  "WeaponDamageNoSchool", S::Damage,
        S::RequiresTarget | S::IsHostileEffect | S::TriggersGCD,
        0, "Weapon damage with no spell school override.");
    add(121, "NormalizedWeaponDmg",  S::Damage,
        S::RequiresTarget | S::IsHostileEffect | S::TriggersGCD,
        0, "Weapon damage normalized to weapon speed.");
    add(67,  "PowerBurn",            S::Damage,
        S::RequiresTarget | S::IsHostileEffect | S::TriggersGCD,
        0, "Burns power resource and deals damage equal to "
        "the burned amount (Mana Burn).");
    return c;
}

WoweeSpellEffectType WoweeSpellEffectTypeLoader::makeHealing(
    const std::string& catalogName) {
    using S = WoweeSpellEffectType;
    WoweeSpellEffectType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t flags,
                    int32_t base, const char* desc) {
        S::Entry e;
        e.effectId = id; e.name = name; e.description = desc;
        e.effectKind = S::Heal;
        e.behaviorFlags = flags;
        e.baseAmount = base;
        e.iconColorRGBA = packRgba(80, 240, 80);   // healing green
        c.entries.push_back(e);
    };
    add(10,  "Heal",          S::RequiresTarget | S::IsBeneficialEffect |
                              S::TriggersGCD, 0,
        "Restore health to target by baseAmount.");
    add(46,  "HealMaxHealth", S::RequiresTarget | S::IsBeneficialEffect,
        0, "Restore target to full health (Lay on Hands).");
    add(116, "HealPct",       S::RequiresTarget | S::IsBeneficialEffect |
                              S::TriggersGCD, 25,
        "Restore health as a percentage of target's max HP.");
    add(118, "ScriptedHeal",  S::RequiresTarget | S::IsBeneficialEffect,
        0, "Custom heal effect, server-script implements the "
        "actual restoration formula.");
    return c;
}

WoweeSpellEffectType WoweeSpellEffectTypeLoader::makeAura(
    const std::string& catalogName) {
    using S = WoweeSpellEffectType;
    WoweeSpellEffectType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t flags,
                    const char* desc) {
        S::Entry e;
        e.effectId = id; e.name = name; e.description = desc;
        e.effectKind = S::Aura;
        e.behaviorFlags = flags;
        e.iconColorRGBA = packRgba(180, 100, 240);   // aura purple
        c.entries.push_back(e);
    };
    add(6,   "ApplyAura",          S::RequiresTarget | S::TriggersGCD,
        "Apply a buff/debuff to target - auraType field "
        "selects the aura behavior (see WAUR catalog).");
    add(35,  "ApplyAuraOnPet",     S::RequiresTarget,
        "Apply aura to caster's pet (Hunter Mend Pet, "
        "Warlock Demonic Empowerment).");
    add(65,  "AreaAuraParty",      S::IsBeneficialEffect,
        "Area aura that affects all party members within "
        "range (Power Word: Fortitude raid buffs).");
    add(82,  "AreaAuraOwner",      S::IsBeneficialEffect,
        "Area aura tied to a totem/object that affects its "
        "owner (Mana Spring totem).");
    add(27,  "PersistentAreaAura", 0,
        "Ground-targeted persistent area aura (Consecration, "
        "Blizzard, Death and Decay).");
    return c;
}

} // namespace pipeline
} // namespace wowee
