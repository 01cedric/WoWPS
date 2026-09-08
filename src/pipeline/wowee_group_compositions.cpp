#include "pipeline/wowee_group_compositions.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'R', 'P'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wgrp";

} // namespace

const WoweeGroupComposition::Entry*
WoweeGroupComposition::findById(uint32_t compId) const {
    for (const auto& e : entries)
        if (e.compId == compId) return &e;
    return nullptr;
}

std::vector<const WoweeGroupComposition::Entry*>
WoweeGroupComposition::findByMap(uint32_t mapId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.mapId == mapId) out.push_back(&e);
    return out;
}

bool WoweeGroupComposition::partyMeetsComp(uint32_t compId,
                                            uint8_t haveTanks,
                                            uint8_t haveHealers,
                                            uint8_t haveDps) const {
    const Entry* e = findById(compId);
    if (!e) return false;
    if (haveTanks < e->requiredTanks) return false;
    if (haveHealers < e->requiredHealers) return false;
    if (haveDps < e->requiredDamageDealers) return false;
    uint8_t total = haveTanks + haveHealers + haveDps;
    if (total < e->minPartySize) return false;
    if (total > e->maxPartySize) return false;
    return true;
}

bool WoweeGroupCompositionLoader::save(const WoweeGroupComposition& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeGroupComposition::Entry& e) {
        writePOD(os, e.compId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.difficultyId);
        writePOD(os, e.requiredTanks);
        writePOD(os, e.requiredHealers);
        writePOD(os, e.requiredDamageDealers);
        writePOD(os, e.minPartySize);
        writePOD(os, e.maxPartySize);
        writePOD(os, e.requireSpec);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeGroupComposition WoweeGroupCompositionLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeGroupComposition>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeGroupComposition::Entry& e) {
        if (!readPOD(is, e.compId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.difficultyId) ||
            !readPOD(is, e.requiredTanks) ||
            !readPOD(is, e.requiredHealers) ||
            !readPOD(is, e.requiredDamageDealers) ||
            !readPOD(is, e.minPartySize) ||
            !readPOD(is, e.maxPartySize) ||
            !readPOD(is, e.requireSpec) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeGroupCompositionLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeGroupComposition WoweeGroupCompositionLoader::makeFiveMan(
    const std::string& catalogName) {
    using G = WoweeGroupComposition;
    WoweeGroupComposition c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint8_t tanks, uint8_t healers, uint8_t dps,
                    uint8_t requireSpec, const char* desc) {
        G::Entry e;
        e.compId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = 1;     // 5-man heroic
        e.requiredTanks = tanks;
        e.requiredHealers = healers;
        e.requiredDamageDealers = dps;
        e.minPartySize = 5;
        e.maxPartySize = 5;
        e.requireSpec = requireSpec;
        e.iconColorRGBA = packRgba(180, 220, 100);   // dungeon green
        c.entries.push_back(e);
    };
    add(1, "Classic5ManTanksHealsDPS", 600, 1, 1, 3, 1,
        "Classic 5-man comp - 1 tank / 1 healer / 3 dps, "
        "spec roles enforced.");
    add(2, "Heavy5ManTrashHeal",       600, 1, 2, 2, 1,
        "Heavy-heal 5-man trash run - 1T/2H/2D for "
        "healing-intensive content.");
    add(3, "RolelessSpeedRun",         600, 0, 0, 5, 0,
        "Roleless 5-man speed run - 5 dps, no spec gate. "
        "Used by speed-run guilds for sub-15min clears.");
    return c;
}

WoweeGroupComposition WoweeGroupCompositionLoader::makeRaid10(
    const std::string& catalogName) {
    using G = WoweeGroupComposition;
    WoweeGroupComposition c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint8_t tanks, uint8_t healers, uint8_t dps,
                    const char* desc) {
        G::Entry e;
        e.compId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = 100;
        e.requiredTanks = tanks;
        e.requiredHealers = healers;
        e.requiredDamageDealers = dps;
        e.minPartySize = 10;
        e.maxPartySize = 10;
        e.requireSpec = 1;
        e.iconColorRGBA = packRgba(220, 80, 100);    // raid red
        c.entries.push_back(e);
    };
    add(100, "Standard10Man", 631, 2, 3, 5,
        "Standard 10-man raid - 2T/3H/5D matches most ICC "
        "10N progression.");
    add(101, "HealingHeavy10Man", 631, 2, 4, 4,
        "Healing-heavy 10-man - 2T/4H/4D for healing-"
        "intensive ICC 10H bosses (Putricide, Sindragosa).");
    add(102, "MeleeStack10Man", 631, 1, 2, 7,
        "Melee-stack 10-man - 1T/2H/7D for melee-cleave "
        "fights with no DPS race (Saurfang heroic exec, "
        "Festergut). Brings extra melee to nuke a single "
        "target; one-tank because no swap mechanic.");
    return c;
}

WoweeGroupComposition WoweeGroupCompositionLoader::makeRaid25(
    const std::string& catalogName) {
    using G = WoweeGroupComposition;
    WoweeGroupComposition c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint8_t tanks, uint8_t healers, uint8_t dps,
                    const char* desc) {
        G::Entry e;
        e.compId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = 101;
        e.requiredTanks = tanks;
        e.requiredHealers = healers;
        e.requiredDamageDealers = dps;
        e.minPartySize = 25;
        e.maxPartySize = 25;
        e.requireSpec = 1;
        e.iconColorRGBA = packRgba(180, 100, 240);    // 25-man purple
        c.entries.push_back(e);
    };
    add(200, "Standard25Man", 631, 2, 6, 17,
        "Standard 25-man raid - 2T/6H/17D matches most ICC "
        "25N progression.");
    add(201, "HealingHeavy25Man", 631, 1, 8, 16,
        "Healing-heavy 25-man - 1T/8H/16D for healing-"
        "intensive ICC 25H Putricide / LK heroic.");
    add(202, "ZergDPS25Man", 631, 0, 4, 21,
        "Zerg DPS 25-man - 0T/4H/21D for tank-immune fights "
        "(Loatheb-style trash piles).");
    return c;
}

} // namespace pipeline
} // namespace wowee
