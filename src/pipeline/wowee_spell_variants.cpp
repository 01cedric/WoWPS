#include "pipeline/wowee_spell_variants.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'P', 'V'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wspv";

} // namespace

const WoweeSpellVariants::Entry*
WoweeSpellVariants::findById(uint32_t variantId) const {
    for (const auto& e : entries)
        if (e.variantId == variantId) return &e;
    return nullptr;
}

std::vector<const WoweeSpellVariants::Entry*>
WoweeSpellVariants::findByBaseSpell(uint32_t baseSpellId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.baseSpellId == baseSpellId) out.push_back(&e);
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->priority > b->priority;
              });
    return out;
}

bool WoweeSpellVariantsLoader::save(const WoweeSpellVariants& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellVariants::Entry& e) {
        writePOD(os, e.variantId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.baseSpellId);
        writePOD(os, e.variantSpellId);
        writePOD(os, e.conditionKind);
        writePOD(os, e.priority);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.conditionValue);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellVariants WoweeSpellVariantsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellVariants>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellVariants::Entry& e) {
        if (!readPOD(is, e.variantId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.baseSpellId) ||
            !readPOD(is, e.variantSpellId) ||
            !readPOD(is, e.conditionKind) ||
            !readPOD(is, e.priority) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.conditionValue) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellVariantsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellVariants WoweeSpellVariantsLoader::makeWarriorStance(
    const std::string& catalogName) {
    using V = WoweeSpellVariants;
    WoweeSpellVariants c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t baseId, uint32_t variantSpell,
                    uint32_t stanceId, uint8_t prio,
                    const char* desc) {
        V::Entry e;
        e.variantId = id; e.name = name; e.description = desc;
        e.baseSpellId = baseId;
        e.variantSpellId = variantSpell;
        e.conditionKind = V::Stance;
        e.priority = prio;
        e.conditionValue = stanceId;
        e.iconColorRGBA = packRgba(220, 60, 60);   // warrior red
        c.entries.push_back(e);
    };
    // Stance spell IDs (3.3.5a):
    // Battle Stance = 2457, Defensive = 71,
    // Berserker = 2458.
    add(1, "HeroicStrikeBerserker", 78, 25286, 2458, 2,
        "Heroic Strike - Berserker Stance variant. "
        "Replaces base ID 78 with variant ID 25286 "
        "(higher rage cost, higher damage). Priority 2 "
        "overrides Battle Stance variant.");
    add(2, "HeroicStrikeBattle", 78, 78, 2457, 1,
        "Heroic Strike - Battle Stance baseline (no "
        "modification). Priority 1 - falls through to "
        "base when in Battle Stance.");
    add(3, "MockingBlowDefensive", 694, 25266, 71, 3,
        "Mocking Blow - Defensive Stance variant adds "
        "AoE taunt component (3-target splash). "
        "Priority 3 (active only when in Defensive).");
    add(4, "PummelBerserker", 6552, 6552, 2458, 1,
        "Pummel - Berserker Stance only (cannot be cast "
        "in other stances). Variant equals base ID "
        "because the spell IS gated to Berserker - "
        "the stance check provides the gate.");
    return c;
}

WoweeSpellVariants WoweeSpellVariantsLoader::makeTalentMod(
    const std::string& catalogName) {
    using V = WoweeSpellVariants;
    WoweeSpellVariants c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t baseId, uint32_t variantSpell,
                    uint32_t talentId, uint8_t prio,
                    const char* desc) {
        V::Entry e;
        e.variantId = id; e.name = name; e.description = desc;
        e.baseSpellId = baseId;
        e.variantSpellId = variantSpell;
        e.conditionKind = V::Talent;
        e.priority = prio;
        e.conditionValue = talentId;
        e.iconColorRGBA = packRgba(180, 100, 240);   // talent purple
        c.entries.push_back(e);
    };
    add(100, "FrostboltBrainFreeze",   116, 44614, 11160, 5,
        "Frostbolt becomes instant-cast when Brain "
        "Freeze proc is active. Talent 11160 = Brain "
        "Freeze passive. Priority 5 - high priority so "
        "the proc takes effect immediately.");
    add(101, "LavaBurstFlameShock", 51505, 51505, 60043, 3,
        "Lava Burst - auto-crit when Flame Shock DoT is "
        "active on target. Talent 60043 = Lava Flows "
        "passive. Same spell ID; the proc replaces the "
        "rolled outcome with guaranteed crit.");
    add(102, "EarthShieldImproved", 974, 974, 16544, 2,
        "Earth Shield - Improved variant adds bonus heal "
        "per charge consumed. Talent 16544 = Improved "
        "Earth Shield. Same spell ID; passive talent "
        "modifies effect magnitude.");
    add(103, "FerociousBiteBerserk", 22568, 22568, 50334, 4,
        "Ferocious Bite - modified by Berserk passive "
        "(50% energy reduction during Berserk). "
        "Talent 50334 = Berserk. Same spell ID; "
        "modifier overrides energy cost.");
    return c;
}

WoweeSpellVariants WoweeSpellVariantsLoader::makeRacial(
    const std::string& catalogName) {
    using V = WoweeSpellVariants;
    WoweeSpellVariants c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t baseId, uint32_t variantSpell,
                    uint32_t raceBit, uint8_t prio,
                    const char* desc) {
        V::Entry e;
        e.variantId = id; e.name = name; e.description = desc;
        e.baseSpellId = baseId;
        e.variantSpellId = variantSpell;
        e.conditionKind = V::Race;
        e.priority = prio;
        e.conditionValue = raceBit;
        e.iconColorRGBA = packRgba(220, 200, 80);   // racial gold
        c.entries.push_back(e);
    };
    // Race bits (WCHC):
    // Human=1, Orc=2, Dwarf=4, NightElf=8,
    // Undead=16, Tauren=32, Gnome=64, Troll=128,
    // BloodElf=512, Draenei=1024.
    add(200, "Stoneform_Dwarf",     20594, 20594,    4, 5,
        "Stoneform - Dwarf-only racial. Removes "
        "bleed/poison/disease, +10%% armor for 8s. "
        "Variant spell ID equals base; the race gate "
        "does the activation.");
    add(201, "WarStomp_Tauren",     20549, 20549,   32, 5,
        "War Stomp - Tauren-only racial. 2-yard AoE "
        "stun for 2s. Castable in any stance/form.");
    add(202, "Berserking_Troll",    26297, 26297,  128, 5,
        "Berserking - Troll-only racial. +10-30%% "
        "haste for 10s, scaling with current health "
        "%.");
    add(203, "WilloftheForsaken",   7744,   7744,   16, 5,
        "Will of the Forsaken - Undead-only racial. "
        "Removes fear/sleep/charm effects, 2-min "
        "cooldown.");
    return c;
}

} // namespace pipeline
} // namespace wowee
