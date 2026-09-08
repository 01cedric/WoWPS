#include "pipeline/wowee_titles.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'I', 'T'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wtit";

} // namespace

const WoweeTitle::Entry* WoweeTitle::findById(uint32_t titleId) const {
    for (const auto& e : entries) if (e.titleId == titleId) return &e;
    return nullptr;
}

const WoweeTitle::Entry* WoweeTitle::findByName(const std::string& n) const {
    for (const auto& e : entries) if (e.name == n) return &e;
    return nullptr;
}

const char* WoweeTitle::categoryName(uint8_t c) {
    switch (c) {
        case Achievement: return "achievement";
        case Pvp:         return "pvp";
        case Raid:        return "raid";
        case ClassTitle:  return "class";
        case Event:       return "event";
        case Profession:  return "profession";
        case Lore:        return "lore";
        case Custom:      return "custom";
        default:          return "unknown";
    }
}

bool WoweeTitleLoader::save(const WoweeTitle& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeTitle::Entry& e) {
        writePOD(os, e.titleId);
        writeStr(os, e.name);
        writeStr(os, e.nameMale);
        writeStr(os, e.nameFemale);
        writeStr(os, e.iconPath);
        writePOD(os, e.prefix);
        writePOD(os, e.category);
        writePOD(os, e.sortOrder);
                       });
}

WoweeTitle WoweeTitleLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeTitle>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeTitle::Entry& e) {
        if (!readPOD(is, e.titleId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.nameMale) ||
            !readStr(is, e.nameFemale) || !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.prefix) ||
            !readPOD(is, e.category) ||
            !readPOD(is, e.sortOrder)) { return false; }
                                  return true;
                              });
}

bool WoweeTitleLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeTitle WoweeTitleLoader::makeStarter(const std::string& catalogName) {
    WoweeTitle c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t cat,
                    uint16_t sort, uint8_t prefix = 0) {
        WoweeTitle::Entry e;
        e.titleId = id; e.name = name; e.category = cat;
        e.sortOrder = sort; e.prefix = prefix;
        c.entries.push_back(e);
    };
    add(1, "the Versatile",  WoweeTitle::Achievement, 100);
    add(2, "Sergeant",       WoweeTitle::Pvp,         200, 1);
    add(3, "Champion",       WoweeTitle::Raid,        300, 1);
    add(4, "the Hallowed",   WoweeTitle::Event,       400);
    return c;
}

WoweeTitle WoweeTitleLoader::makePvp(const std::string& catalogName) {
    WoweeTitle c;
    c.name = catalogName;
    // Classic Honor System rank ladder (14 tiers per faction).
    // Most ranks share names across both factions; only the
    // ranked-officer titles (Knight / Stone Guard etc.) split.
    auto add = [&](uint32_t id, const char* name, uint16_t sort) {
        WoweeTitle::Entry e;
        e.titleId = id; e.name = name;
        e.category = WoweeTitle::Pvp;
        e.sortOrder = sort; e.prefix = 1;
        c.entries.push_back(e);
    };
    // Alliance ladder.
    add(101, "Private",            10);
    add(102, "Corporal",           20);
    add(103, "Sergeant",           30);
    add(104, "Master Sergeant",    40);
    add(105, "Sergeant Major",     50);
    add(106, "Knight",             60);
    add(107, "Knight-Lieutenant",  70);
    add(108, "Knight-Captain",     80);
    add(109, "Knight-Champion",    90);
    add(110, "Lieutenant Commander", 100);
    add(111, "Commander",          110);
    add(112, "Marshal",            120);
    add(113, "Field Marshal",      130);
    add(114, "Grand Marshal",      140);
    // Horde ladder (parallel ranks 6-14).
    add(201, "Scout",              15);
    add(202, "Grunt",              25);
    add(203, "Sergeant",           35);
    add(204, "Senior Sergeant",    45);
    add(205, "First Sergeant",     55);
    add(206, "Stone Guard",        65);
    add(207, "Blood Guard",        75);
    add(208, "Legionnaire",        85);
    add(209, "Centurion",          95);
    add(210, "Champion",           105);
    add(211, "Lieutenant General", 115);
    add(212, "General",            125);
    add(213, "Warlord",            135);
    add(214, "High Warlord",       145);
    return c;
}

WoweeTitle WoweeTitleLoader::makeAchievement(const std::string& catalogName) {
    WoweeTitle c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint16_t sort,
                    uint8_t prefix = 0, uint8_t cat = WoweeTitle::Achievement) {
        WoweeTitle::Entry e;
        e.titleId = id; e.name = name; e.category = cat;
        e.sortOrder = sort; e.prefix = prefix;
        c.entries.push_back(e);
    };
    // titleId=1 + name="the Versatile" matches WACH.makeMeta
    // achievement 250 (Jack of All Trades).
    add(1, "the Versatile",      100);
    add(2, "the Explorer",       200);
    add(3, "Loremaster",         300, 1);
    add(4, "the Insane",         400);
    add(5, "the Patient",        500);
    add(6, "Salty",              600);
    // Profession capstone titles.
    add(10, "Master Locksmith",  1000, 0, WoweeTitle::Profession);
    add(11, "Master Chef",       1100, 0, WoweeTitle::Profession);
    return c;
}

} // namespace pipeline
} // namespace wowee
