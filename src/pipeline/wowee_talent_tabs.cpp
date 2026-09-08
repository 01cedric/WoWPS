#include "pipeline/wowee_talent_tabs.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'L', 'E'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wtle";

constexpr uint32_t CLS_WARRIOR = 1u << 0;
constexpr uint32_t CLS_PALADIN = 1u << 1;
constexpr uint32_t CLS_MAGE    = 1u << 7;

} // namespace

const WoweeTalentTab::Entry*
WoweeTalentTab::findById(uint32_t tabId) const {
    for (const auto& e : entries)
        if (e.tabId == tabId) return &e;
    return nullptr;
}

std::vector<const WoweeTalentTab::Entry*>
WoweeTalentTab::findByClass(uint32_t classBit) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.classMask & classBit) out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->displayOrder < b->displayOrder;
              });
    return out;
}

const char* WoweeTalentTab::roleHintName(uint8_t r) {
    switch (r) {
        case DPS:      return "dps";
        case Tank:     return "tank";
        case Healer:   return "healer";
        case Hybrid:   return "hybrid";
        case PetClass: return "pet";
        default:       return "unknown";
    }
}

bool WoweeTalentTabLoader::save(const WoweeTalentTab& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeTalentTab::Entry& e) {
        writePOD(os, e.tabId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.classMask);
        writePOD(os, e.displayOrder);
        writePOD(os, e.roleHint);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writeStr(os, e.iconPath);
        writeStr(os, e.backgroundFile);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeTalentTab WoweeTalentTabLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeTalentTab>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeTalentTab::Entry& e) {
        if (!readPOD(is, e.tabId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.classMask) ||
            !readPOD(is, e.displayOrder) ||
            !readPOD(is, e.roleHint) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1)) { return false; }
        if (!readStr(is, e.iconPath) ||
            !readStr(is, e.backgroundFile)) { return false; }
        if (!readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeTalentTabLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeTalentTab WoweeTalentTabLoader::makeWarrior(
    const std::string& catalogName) {
    using T = WoweeTalentTab;
    WoweeTalentTab c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t order,
                    uint8_t role, const char* icon, const char* bg,
                    const char* desc) {
        T::Entry e;
        e.tabId = id; e.name = name; e.description = desc;
        e.classMask = CLS_WARRIOR;
        e.displayOrder = order;
        e.roleHint = role;
        e.iconPath = icon;
        e.backgroundFile = bg;
        e.iconColorRGBA = packRgba(220, 60, 60);   // warrior red
        c.entries.push_back(e);
    };
    add(161, "Arms",       0, T::DPS,
        "Interface\\Icons\\Ability_Rogue_Eviscerate",
        "WarriorArms",
        "Arms - two-handed weapon mastery, Mortal Strike DPS spec.");
    add(164, "Fury",       1, T::DPS,
        "Interface\\Icons\\Ability_Warrior_InnerRage",
        "WarriorFury",
        "Fury - dual-wield berserker DPS spec.");
    add(163, "Protection", 2, T::Tank,
        "Interface\\Icons\\INV_Shield_06",
        "WarriorProtection",
        "Protection - shield-wielding tank spec.");
    return c;
}

WoweeTalentTab WoweeTalentTabLoader::makeMage(
    const std::string& catalogName) {
    using T = WoweeTalentTab;
    WoweeTalentTab c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t order,
                    const char* icon, const char* bg,
                    const char* desc) {
        T::Entry e;
        e.tabId = id; e.name = name; e.description = desc;
        e.classMask = CLS_MAGE;
        e.displayOrder = order;
        e.roleHint = T::DPS;
        e.iconPath = icon;
        e.backgroundFile = bg;
        e.iconColorRGBA = packRgba(80, 140, 240);   // mage blue
        c.entries.push_back(e);
    };
    add(81, "Arcane", 0,
        "Interface\\Icons\\Spell_Holy_MagicalSentry",
        "MageArcane",
        "Arcane - burst-mana spec around Arcane Blast scaling.");
    add(41, "Fire",   1,
        "Interface\\Icons\\Spell_Fire_FireBolt02",
        "MageFire",
        "Fire - crit-focused spec around Pyroblast / Combustion.");
    add(61, "Frost",  2,
        "Interface\\Icons\\Spell_Frost_FrostBolt02",
        "MageFrost",
        "Frost - control + sustained-damage spec.");
    return c;
}

WoweeTalentTab WoweeTalentTabLoader::makePaladin(
    const std::string& catalogName) {
    using T = WoweeTalentTab;
    WoweeTalentTab c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t order,
                    uint8_t role, const char* icon, const char* bg,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        T::Entry e;
        e.tabId = id; e.name = name; e.description = desc;
        e.classMask = CLS_PALADIN;
        e.displayOrder = order;
        e.roleHint = role;
        e.iconPath = icon;
        e.backgroundFile = bg;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(382, "Holy",        0, T::Healer,
        "Interface\\Icons\\Spell_Holy_HolyBolt",
        "PaladinHoly",
        240, 240, 200, "Holy - single-target healing spec.");
    add(383, "Protection",  1, T::Tank,
        "Interface\\Icons\\Spell_Holy_DevotionAura",
        "PaladinProtection",
        220, 220, 180, "Protection - shield + holy power tank spec.");
    add(381, "Retribution", 2, T::DPS,
        "Interface\\Icons\\Spell_Holy_AuraOfLight",
        "PaladinRetribution",
        240, 200, 100, "Retribution - two-handed melee DPS spec.");
    return c;
}

} // namespace pipeline
} // namespace wowee
