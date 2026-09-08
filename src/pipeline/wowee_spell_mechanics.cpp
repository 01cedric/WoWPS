#include "pipeline/wowee_spell_mechanics.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'M', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wsmc";

} // namespace

const WoweeSpellMechanic::Entry*
WoweeSpellMechanic::findById(uint32_t mechanicId) const {
    for (const auto& e : entries)
        if (e.mechanicId == mechanicId) return &e;
    return nullptr;
}

const char* WoweeSpellMechanic::drCategoryName(uint8_t c) {
    switch (c) {
        case DRNone:       return "none";
        case DRStun:       return "stun";
        case DRDisorient:  return "disorient";
        case DRSilence:    return "silence";
        case DRRoot:       return "root";
        case DRPolymorph:  return "polymorph";
        case DRControlled: return "controlled";
        case DRMisc:       return "misc";
        default:           return "unknown";
    }
}

const char* WoweeSpellMechanic::dispelTypeName(uint8_t d) {
    switch (d) {
        case DispelNone:    return "none";
        case DispelMagic:   return "magic";
        case DispelCurse:   return "curse";
        case DispelDisease: return "disease";
        case DispelPoison:  return "poison";
        case DispelEnrage:  return "enrage";
        case DispelStealth: return "stealth";
        default:            return "unknown";
    }
}

bool WoweeSpellMechanicLoader::save(const WoweeSpellMechanic& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellMechanic::Entry& e) {
        writePOD(os, e.mechanicId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.breaksOnDamage);
        writePOD(os, e.canBeDispelled);
        writePOD(os, e.drCategory);
        writePOD(os, e.dispelType);
        writePOD(os, e.defaultDurationMs);
        writePOD(os, e.maxStacks);
        writePadding(os, 3);
        writePOD(os, e.conflictsMask);
                       });
}

WoweeSpellMechanic WoweeSpellMechanicLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellMechanic>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellMechanic::Entry& e) {
        if (!readPOD(is, e.mechanicId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.breaksOnDamage) ||
            !readPOD(is, e.canBeDispelled) ||
            !readPOD(is, e.drCategory) ||
            !readPOD(is, e.dispelType) ||
            !readPOD(is, e.defaultDurationMs) ||
            !readPOD(is, e.maxStacks)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.conflictsMask)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellMechanicLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellMechanic WoweeSpellMechanicLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellMechanic c;
    c.name = catalogName;
    {
        WoweeSpellMechanic::Entry e;
        e.mechanicId = 1; e.name = "Stun";
        e.description = "Target is stunned and cannot move, attack, "
                         "or cast.";
        e.iconPath = "Interface/Icons/Spell_Magic_PolymorphRabbit.blp";
        e.breaksOnDamage = 0;
        e.canBeDispelled = 0;
        e.drCategory = WoweeSpellMechanic::DRStun;
        e.dispelType = WoweeSpellMechanic::DispelNone;
        e.defaultDurationMs = 4000;
        e.maxStacks = 1;
        c.entries.push_back(e);
    }
    {
        WoweeSpellMechanic::Entry e;
        e.mechanicId = 2; e.name = "Silence";
        e.description = "Target cannot cast spells.";
        e.iconPath = "Interface/Icons/Spell_Shadow_ImpPhaseShift.blp";
        e.breaksOnDamage = 0;
        e.canBeDispelled = 1;
        e.drCategory = WoweeSpellMechanic::DRSilence;
        e.dispelType = WoweeSpellMechanic::DispelMagic;
        e.defaultDurationMs = 3000;
        e.maxStacks = 1;
        c.entries.push_back(e);
    }
    {
        WoweeSpellMechanic::Entry e;
        e.mechanicId = 3; e.name = "Snare";
        e.description = "Target's movement speed reduced.";
        e.iconPath = "Interface/Icons/Spell_Frost_FrostShock.blp";
        e.breaksOnDamage = 0;
        e.canBeDispelled = 1;
        e.drCategory = WoweeSpellMechanic::DRRoot;
        e.dispelType = WoweeSpellMechanic::DispelMagic;
        e.defaultDurationMs = 8000;
        e.maxStacks = 1;
        c.entries.push_back(e);
    }
    return c;
}

WoweeSpellMechanic WoweeSpellMechanicLoader::makeHardCC(
    const std::string& catalogName) {
    WoweeSpellMechanic c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t breaks,
                    uint8_t dispellable, uint8_t dr,
                    uint8_t dispelKind, uint32_t durMs,
                    uint32_t conflicts, const char* desc) {
        WoweeSpellMechanic::Entry e;
        e.mechanicId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_") +
                      name + ".blp";
        e.breaksOnDamage = breaks;
        e.canBeDispelled = dispellable;
        e.drCategory = dr;
        e.dispelType = dispelKind;
        e.defaultDurationMs = durMs;
        e.maxStacks = 1;
        e.conflictsMask = conflicts;
        c.entries.push_back(e);
    };
    // Hard-CC mechanics. conflictsMask uses bits = mechanicId
    // shifted left - so id=10 (Stun) has bit 0x400 (1<<10)
    // referenced by anything that conflicts with Stun.
    add(10, "Stun",       0, 0, WoweeSpellMechanic::DRStun,
        WoweeSpellMechanic::DispelNone,    4000, 0,
        "Target stunned - no actions for 4 seconds.");
    add(11, "Polymorph",  1, 1, WoweeSpellMechanic::DRPolymorph,
        WoweeSpellMechanic::DispelMagic,   8000, (1u << 10),
        "Transformed into a sheep - breaks on damage.");
    add(12, "Sleep",      1, 1, WoweeSpellMechanic::DRPolymorph,
        WoweeSpellMechanic::DispelMagic,   6000, (1u << 11),
        "Target sleeping - breaks on damage.");
    add(13, "Fear",       1, 1, WoweeSpellMechanic::DRDisorient,
        WoweeSpellMechanic::DispelMagic,   8000, 0,
        "Target flees in random direction.");
    add(14, "Knockback",  0, 0, WoweeSpellMechanic::DRStun,
        WoweeSpellMechanic::DispelNone,    1500, (1u << 10),
        "Target launched backward - brief knockdown.");
    return c;
}

WoweeSpellMechanic WoweeSpellMechanicLoader::makeRoots(
    const std::string& catalogName) {
    WoweeSpellMechanic c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t breaks,
                    uint8_t maxStacks, uint32_t durMs,
                    const char* desc) {
        WoweeSpellMechanic::Entry e;
        e.mechanicId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_Nature_") +
                      name + ".blp";
        e.breaksOnDamage = breaks;
        e.canBeDispelled = 1;
        e.drCategory = WoweeSpellMechanic::DRRoot;
        e.dispelType = WoweeSpellMechanic::DispelMagic;
        e.defaultDurationMs = durMs;
        e.maxStacks = maxStacks;
        c.entries.push_back(e);
    };
    add(20, "Root",          0, 1,  6000,
        "Target rooted in place - cannot move.");
    add(21, "Snare",         0, 1,  8000,
        "Movement speed reduced by 50%.");
    add(22, "Slow",          0, 5, 10000,
        "Stacking slow - each stack adds 10% slow up to 50%.");
    add(23, "GroundPin",     1, 1,  3000,
        "Pinned to the ground - breaks on damage.");
    return c;
}

} // namespace pipeline
} // namespace wowee
