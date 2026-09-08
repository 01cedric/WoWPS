#include "pipeline/wowee_action_bars.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'C', 'T'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wact";

constexpr uint32_t CLS_WARRIOR = 1u << 0;
constexpr uint32_t CLS_HUNTER  = 1u << 2;
constexpr uint32_t CLS_MAGE    = 1u << 7;

} // namespace

const WoweeActionBar::Entry*
WoweeActionBar::findById(uint32_t bindingId) const {
    for (const auto& e : entries)
        if (e.bindingId == bindingId) return &e;
    return nullptr;
}

std::vector<const WoweeActionBar::Entry*>
WoweeActionBar::findByClass(uint32_t classBit, uint8_t barMode) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if ((e.classMask & classBit) == 0) continue;
        if (e.barMode != barMode) continue;
        out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->buttonSlot < b->buttonSlot;
              });
    return out;
}

const char* WoweeActionBar::barModeName(uint8_t m) {
    switch (m) {
        case Main:    return "main";
        case Pet:     return "pet";
        case Vehicle: return "vehicle";
        case Stance1: return "stance1";
        case Stance2: return "stance2";
        case Stance3: return "stance3";
        case Custom:  return "custom";
        default:      return "unknown";
    }
}

bool WoweeActionBarLoader::save(const WoweeActionBar& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeActionBar::Entry& e) {
        writePOD(os, e.bindingId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.classMask);
        writePOD(os, e.spellId);
        writePOD(os, e.itemId);
        writePOD(os, e.buttonSlot);
        writePOD(os, e.barMode);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeActionBar WoweeActionBarLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeActionBar>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeActionBar::Entry& e) {
        if (!readPOD(is, e.bindingId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.classMask) ||
            !readPOD(is, e.spellId) ||
            !readPOD(is, e.itemId) ||
            !readPOD(is, e.buttonSlot) ||
            !readPOD(is, e.barMode) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeActionBarLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeActionBar WoweeActionBarLoader::makeWarrior(
    const std::string& catalogName) {
    using A = WoweeActionBar;
    WoweeActionBar c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t slot,
                    uint32_t spell, const char* desc) {
        A::Entry e;
        e.bindingId = id; e.name = name; e.description = desc;
        e.classMask = CLS_WARRIOR;
        e.spellId = spell;
        e.buttonSlot = slot;
        e.barMode = A::Main;
        e.iconColorRGBA = packRgba(220, 60, 60);    // warrior red
        c.entries.push_back(e);
    };
    // Warrior starter bindings - 10 abilities on slots 0-9.
    add(1,  "WarriorBtn0_HeroicStrike",  0, 78,
        "Heroic Strike - replaces next melee swing.");
    add(2,  "WarriorBtn1_Charge",        1, 100,
        "Charge - close gap from out of combat.");
    add(3,  "WarriorBtn2_Rend",          2, 772,
        "Rend - physical bleed DoT.");
    add(4,  "WarriorBtn3_ThunderClap",   3, 6343,
        "Thunder Clap - AoE damage + attack speed slow.");
    add(5,  "WarriorBtn4_BattleShout",   4, 6673,
        "Battle Shout - party-wide attack power buff.");
    add(6,  "WarriorBtn5_SunderArmor",   5, 7386,
        "Sunder Armor - armor reduction stack.");
    add(7,  "WarriorBtn6_MockingBlow",   6, 694,
        "Mocking Blow - taunt single target.");
    add(8,  "WarriorBtn7_Hamstring",     7, 1715,
        "Hamstring - movement-speed slow.");
    add(9,  "WarriorBtn8_OverPower",     8, 7384,
        "Overpower - instant strike after enemy dodge.");
    add(10, "WarriorBtn9_VictoryRush",   9, 34428,
        "Victory Rush - instant strike after a kill.");
    return c;
}

WoweeActionBar WoweeActionBarLoader::makeMage(
    const std::string& catalogName) {
    using A = WoweeActionBar;
    WoweeActionBar c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t slot,
                    uint32_t spell, const char* desc) {
        A::Entry e;
        e.bindingId = id; e.name = name; e.description = desc;
        e.classMask = CLS_MAGE;
        e.spellId = spell;
        e.buttonSlot = slot;
        e.barMode = A::Main;
        e.iconColorRGBA = packRgba(80, 140, 240);   // mage blue
        c.entries.push_back(e);
    };
    add(100, "MageBtn0_Fireball",     0, 133,
        "Fireball - primary fire-school spell.");
    add(101, "MageBtn1_Frostbolt",    1, 116,
        "Frostbolt - primary frost-school spell with chill.");
    add(102, "MageBtn2_FrostNova",    2, 122,
        "Frost Nova - AoE root and minor damage.");
    add(103, "MageBtn3_Polymorph",    3, 118,
        "Polymorph - single-target sheep CC.");
    add(104, "MageBtn4_MageArmor",    4, 6117,
        "Mage Armor - passive resistance + mana regen.");
    add(105, "MageBtn5_ArcaneIntellect",5, 1459,
        "Arcane Intellect - party-wide Intellect buff.");
    add(106, "MageBtn6_Counterspell", 6, 2139,
        "Counterspell - interrupt + 8s school lockout.");
    add(107, "MageBtn7_Blink",        7, 1953,
        "Blink - 20y forward teleport, breaks roots.");
    add(108, "MageBtn8_FireBlast",    8, 2136,
        "Fire Blast - instant fire damage, off-GCD trigger.");
    add(109, "MageBtn9_ConjureWater", 9, 5504,
        "Conjure Water - create mana-restoring water stack.");
    return c;
}

WoweeActionBar WoweeActionBarLoader::makeHunterPet(
    const std::string& catalogName) {
    using A = WoweeActionBar;
    WoweeActionBar c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t slot,
                    uint32_t spell, const char* desc) {
        A::Entry e;
        e.bindingId = id; e.name = name; e.description = desc;
        e.classMask = CLS_HUNTER;
        e.spellId = spell;
        e.buttonSlot = slot;
        // Pet bar - separate from main bar.
        e.barMode = A::Pet;
        e.iconColorRGBA = packRgba(100, 200, 100);   // pet green
        c.entries.push_back(e);
    };
    // Hunter pet bar - 10 standard slots on the dedicated
    // Pet bar mode (slots 0-9).
    add(200, "PetBtn0_Attack",        0,  2649,
        "Attack - sic pet on current target.");
    add(201, "PetBtn1_Follow",        1, 23110,
        "Follow - recall pet to stand behind owner.");
    add(202, "PetBtn2_Stay",          2,  6991,
        "Stay - hold position, no auto-attack.");
    add(203, "PetBtn3_Aggressive",    3,  2106,
        "Aggressive stance - auto-engage nearby hostiles.");
    add(204, "PetBtn4_Defensive",     4,  2104,
        "Defensive stance - retaliate when hit.");
    add(205, "PetBtn5_Passive",       5,  2105,
        "Passive stance - never auto-engage.");
    add(206, "PetBtn6_Bite",          6, 17253,
        "Bite - pet's primary damage ability.");
    add(207, "PetBtn7_Claw",          7, 16827,
        "Claw - alt damage ability for cat/raptor families.");
    add(208, "PetBtn8_Growl",         8,  2649,
        "Growl - pet's taunt ability.");
    add(209, "PetBtn9_DismissPet",    9,  2641,
        "Dismiss Pet - return active pet to the stable.");
    return c;
}

} // namespace pipeline
} // namespace wowee
