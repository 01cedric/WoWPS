#include "pipeline/wowee_skills.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'K', 'L'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wskl";

} // namespace

const WoweeSkill::Entry* WoweeSkill::findById(uint32_t skillId) const {
    for (const auto& e : entries) {
        if (e.skillId == skillId) return &e;
    }
    return nullptr;
}

const char* WoweeSkill::categoryName(uint8_t c) {
    switch (c) {
        case Weapon:              return "weapon";
        case Class:               return "class";
        case Profession:          return "profession";
        case SecondaryProfession: return "secondary";
        case Language:            return "language";
        case ArmorProficiency:    return "armor";
        case Riding:              return "riding";
        case WeaponSpec:          return "weapon-spec";
        default:                  return "unknown";
    }
}

bool WoweeSkillLoader::save(const WoweeSkill& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSkill::Entry& e) {
        writePOD(os, e.skillId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.categoryId);
        writePOD(os, e.canTrain);
        writePadding(os, 2);
        writePOD(os, e.maxRank);
        writePOD(os, e.rankPerLevel);
        writeStr(os, e.iconPath);
                       });
}

WoweeSkill WoweeSkillLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSkill>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSkill::Entry& e) {
        if (!readPOD(is, e.skillId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.categoryId) ||
            !readPOD(is, e.canTrain)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
        if (!readPOD(is, e.maxRank) ||
            !readPOD(is, e.rankPerLevel)) { return false; }
        if (!readStr(is, e.iconPath)) { return false; }
                                  return true;
                              });
}

bool WoweeSkillLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSkill WoweeSkillLoader::makeStarter(const std::string& catalogName) {
    WoweeSkill c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t cat,
                   uint16_t maxRank, uint16_t perLevel,
                   uint8_t train) {
        WoweeSkill::Entry e;
        e.skillId = id; e.name = name;
        e.categoryId = cat; e.maxRank = maxRank;
        e.rankPerLevel = perLevel; e.canTrain = train;
        c.entries.push_back(e);
    };
    add(43,  "Swords",         WoweeSkill::Weapon,             300, 5, 1);
    add(98,  "Common",         WoweeSkill::Language,             1, 0, 0);
    add(129, "First Aid",      WoweeSkill::SecondaryProfession, 300, 0, 1);
    // SkillId 186 = Mining, 633 = Lockpicking - the canonical
    // values that WGOT.makeGather and WLCK.makeDungeon already
    // reference.
    add(186, "Mining",         WoweeSkill::Profession,         300, 0, 1);
    add(633, "Lockpicking",    WoweeSkill::SecondaryProfession, 300, 0, 1);
    return c;
}

WoweeSkill WoweeSkillLoader::makeProfessions(const std::string& catalogName) {
    WoweeSkill c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t cat) {
        WoweeSkill::Entry e;
        e.skillId = id; e.name = name;
        e.categoryId = cat; e.maxRank = 300; e.canTrain = 1;
        c.entries.push_back(e);
    };
    // Primary professions (canonical SkillLine IDs).
    add(164, "Blacksmithing",  WoweeSkill::Profession);
    add(165, "Leatherworking", WoweeSkill::Profession);
    add(171, "Alchemy",        WoweeSkill::Profession);
    add(182, "Herbalism",      WoweeSkill::Profession);
    add(186, "Mining",         WoweeSkill::Profession);
    add(197, "Tailoring",      WoweeSkill::Profession);
    add(202, "Engineering",    WoweeSkill::Profession);
    add(333, "Enchanting",     WoweeSkill::Profession);
    add(393, "Skinning",       WoweeSkill::Profession);
    // Secondary professions.
    add(129, "First Aid",      WoweeSkill::SecondaryProfession);
    add(185, "Cooking",        WoweeSkill::SecondaryProfession);
    add(356, "Fishing",        WoweeSkill::SecondaryProfession);
    return c;
}

WoweeSkill WoweeSkillLoader::makeWeapons(const std::string& catalogName) {
    WoweeSkill c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name) {
        WoweeSkill::Entry e;
        e.skillId = id; e.name = name;
        e.categoryId = WoweeSkill::Weapon;
        e.maxRank = 300;        // matches character-level cap × 5
        e.rankPerLevel = 5;     // weapon skill auto-grows by 5/level
        e.canTrain = 0;         // weapons train via use, not trainer
        c.entries.push_back(e);
    };
    // Canonical SkillLine IDs from WoW classic.
    add( 43, "Swords");
    add( 44, "Axes");
    add( 45, "Bows");
    add( 46, "Guns");
    add( 54, "Maces");
    add( 55, "Two-Handed Swords");
    add( 95, "Defense");
    add(118, "Daggers");
    add(136, "Staves");
    add(160, "Two-Handed Maces");
    add(172, "Two-Handed Axes");
    add(173, "Polearms");
    add(176, "Thrown");
    add(226, "Crossbows");
    add(228, "Wands");
    add(473, "Fist Weapons");
    return c;
}

} // namespace pipeline
} // namespace wowee
