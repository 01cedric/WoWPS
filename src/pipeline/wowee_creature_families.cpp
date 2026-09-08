#include "pipeline/wowee_creature_families.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'E', 'F'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcef";

} // namespace

const WoweeCreatureFamily::Entry*
WoweeCreatureFamily::findById(uint32_t familyId) const {
    for (const auto& e : entries)
        if (e.familyId == familyId) return &e;
    return nullptr;
}

const char* WoweeCreatureFamily::familyKindName(uint8_t k) {
    switch (k) {
        case Beast:     return "beast";
        case Demon:     return "demon";
        case Undead:    return "undead";
        case Elemental: return "elemental";
        case NotPet:    return "not-pet";
        case Exotic:    return "exotic";
        default:        return "unknown";
    }
}

const char* WoweeCreatureFamily::petTalentTreeName(uint8_t t) {
    switch (t) {
        case TreeNone: return "none";
        case Ferocity: return "ferocity";
        case Tenacity: return "tenacity";
        case Cunning:  return "cunning";
        default:       return "unknown";
    }
}

bool WoweeCreatureFamilyLoader::save(const WoweeCreatureFamily& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCreatureFamily::Entry& e) {
        writePOD(os, e.familyId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.familyKind);
        writePOD(os, e.petTalentTree);
        writePOD(os, e.minLevelForTame);
        writePOD(os, e.pad0);
        writePOD(os, e.skillLine);
        writePOD(os, e.petFoodTypes);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeCreatureFamily WoweeCreatureFamilyLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCreatureFamily>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCreatureFamily::Entry& e) {
        if (!readPOD(is, e.familyId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.familyKind) ||
            !readPOD(is, e.petTalentTree) ||
            !readPOD(is, e.minLevelForTame) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.skillLine) ||
            !readPOD(is, e.petFoodTypes) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeCreatureFamilyLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeCreatureFamily WoweeCreatureFamilyLoader::makeStarter(
    const std::string& catalogName) {
    using F = WoweeCreatureFamily;
    WoweeCreatureFamily c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t tree, uint8_t minLvl, uint32_t skill,
                    uint32_t foods,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        F::Entry e;
        e.familyId = id; e.name = name; e.description = desc;
        e.familyKind = kind;
        e.petTalentTree = tree;
        e.minLevelForTame = minLvl;
        e.skillLine = skill;
        e.petFoodTypes = foods;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "Bear",   F::Beast, F::Tenacity, 10,  208,
        F::Meat | F::Fish | F::Fruit | F::Fungus | F::Raw,
        140, 100,  60, "Bear - tenacity tank pet, omnivore.");
    add(2, "Cat",    F::Beast, F::Ferocity, 10,  209,
        F::Meat | F::Fish | F::Raw,
        220, 180,  60, "Cat - ferocity DPS pet, carnivore.");
    add(3, "Wolf",   F::Beast, F::Ferocity, 10,  210,
        F::Meat | F::Raw,
        180, 180, 180, "Wolf - ferocity DPS pet, meat-only.");
    add(4, "Boar",   F::Beast, F::Tenacity, 10,  211,
        F::Meat | F::Fruit | F::Fungus | F::Bread,
        160, 120, 100, "Boar - tenacity tank pet, ravenous omnivore.");
    add(5, "Crab",   F::Beast, F::Tenacity, 10,  212,
        F::Fish | F::Meat | F::Raw,
        120, 180, 200, "Crab - tenacity tank pet, prefers fish.");
    return c;
}

WoweeCreatureFamily WoweeCreatureFamilyLoader::makeFerocity(
    const std::string& catalogName) {
    using F = WoweeCreatureFamily;
    WoweeCreatureFamily c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t minLvl,
                    uint32_t skill, uint32_t foods,
                    const char* desc) {
        F::Entry e;
        e.familyId = id; e.name = name; e.description = desc;
        e.familyKind = F::Beast;
        e.petTalentTree = F::Ferocity;
        e.minLevelForTame = minLvl;
        e.skillLine = skill;
        e.petFoodTypes = foods;
        e.iconColorRGBA = packRgba(220, 60, 60);   // red - DPS
        c.entries.push_back(e);
    };
    add(100, "Cat",       10, 209, F::Meat | F::Fish | F::Raw,
        "Cat - fast attack speed, claws hit hard.");
    add(101, "Wolf",      10, 210, F::Meat | F::Raw,
        "Wolf - Furious Howl pack buff (10% AP raid-wide).");
    add(102, "Raptor",    10, 213, F::Meat | F::Raw,
        "Raptor - bleed effect on melee strikes.");
    add(103, "Devilsaur", 30, 214, F::Meat | F::Raw,
        "Devilsaur - Monstrous Bite armor reduction.");
    return c;
}

WoweeCreatureFamily WoweeCreatureFamilyLoader::makeExotic(
    const std::string& catalogName) {
    using F = WoweeCreatureFamily;
    WoweeCreatureFamily c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t tree,
                    uint8_t minLvl, uint32_t skill, uint32_t foods,
                    const char* desc) {
        F::Entry e;
        e.familyId = id; e.name = name; e.description = desc;
        e.familyKind = F::Exotic;
        e.petTalentTree = tree;
        e.minLevelForTame = minLvl;
        e.skillLine = skill;
        e.petFoodTypes = foods;
        e.iconColorRGBA = packRgba(200, 100, 240);   // purple - exotic
        c.entries.push_back(e);
    };
    add(200, "Worm",       F::Tenacity, 50, 220,
        F::Meat | F::Fungus | F::Raw,
        "Worm - exotic tenacity, Acid Spit reduces target armor.");
    add(201, "Devilsaur",  F::Ferocity, 60, 214,
        F::Meat | F::Raw,
        "Devilsaur - exotic, Monstrous Bite + huge HP pool.");
    add(202, "Chimaera",   F::Cunning,  60, 221,
        F::Meat | F::Raw,
        "Chimaera - exotic cunning, Froststorm Breath chain frost.");
    add(203, "CoreHound",  F::Ferocity, 60, 222,
        F::Meat | F::Raw,
        "Core Hound - exotic, Lava Breath + Ancient Hysteria "
        "raid bloodlust.");
    return c;
}

} // namespace pipeline
} // namespace wowee
