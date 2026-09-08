#include "pipeline/wowee_companions.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'M', 'P'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcmp";

} // namespace

const WoweeCompanion::Entry*
WoweeCompanion::findById(uint32_t companionId) const {
    for (const auto& e : entries)
        if (e.companionId == companionId) return &e;
    return nullptr;
}

const char* WoweeCompanion::companionKindName(uint8_t k) {
    switch (k) {
        case Critter:         return "critter";
        case Mechanical:      return "mechanical";
        case DragonHatchling: return "dragon";
        case Demonic:         return "demonic";
        case Spectral:        return "spectral";
        case Elemental:       return "elemental";
        case Plush:           return "plush";
        case UndeadCritter:   return "undead";
        default:              return "unknown";
    }
}

const char* WoweeCompanion::rarityName(uint8_t r) {
    switch (r) {
        case Common:   return "common";
        case Uncommon: return "uncommon";
        case Rare:     return "rare";
        case Epic:     return "epic";
        default:       return "unknown";
    }
}

const char* WoweeCompanion::factionRestrictionName(uint8_t f) {
    switch (f) {
        case AnyFaction:   return "any";
        case AllianceOnly: return "alliance";
        case HordeOnly:    return "horde";
        default:           return "unknown";
    }
}

bool WoweeCompanionLoader::save(const WoweeCompanion& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCompanion::Entry& e) {
        writePOD(os, e.companionId);
        writePOD(os, e.creatureId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.companionKind);
        writePOD(os, e.rarity);
        writePOD(os, e.factionRestriction);
        writePadding(os, 1);
        writePOD(os, e.learnSpellId);
        writePOD(os, e.itemId);
        writePOD(os, e.idleSoundId);
                       });
}

WoweeCompanion WoweeCompanionLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCompanion>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCompanion::Entry& e) {
        if (!readPOD(is, e.companionId) ||
            !readPOD(is, e.creatureId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.companionKind) ||
            !readPOD(is, e.rarity) ||
            !readPOD(is, e.factionRestriction)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.learnSpellId) ||
            !readPOD(is, e.itemId) ||
            !readPOD(is, e.idleSoundId)) { return false; }
                                  return true;
                              });
}

bool WoweeCompanionLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeCompanion WoweeCompanionLoader::makeStarter(
    const std::string& catalogName) {
    WoweeCompanion c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t creature, const char* name,
                    uint32_t spellId, uint32_t itemId,
                    uint8_t kind, const char* desc) {
        WoweeCompanion::Entry e;
        e.companionId = id; e.creatureId = creature;
        e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_Pet_") +
                      name + ".blp";
        e.companionKind = kind;
        e.rarity = WoweeCompanion::Common;
        e.learnSpellId = spellId;
        e.itemId = itemId;
        c.entries.push_back(e);
    };
    add(1, 7560, "MechanicalSquirrel", 4055, 4401,
        WoweeCompanion::Mechanical,
        "Engineering-built mechanical squirrel - clicks "
        "and chitters as it follows.");
    add(2, 7349, "Cat",                10684, 8491,
        WoweeCompanion::Critter,
        "Generic alley cat - purrs when stationary.");
    add(3, 7547, "PrairieDog",         9484, 7560,
        WoweeCompanion::Critter,
        "Tan prairie dog - pops up to look around "
        "every few seconds.");
    return c;
}

WoweeCompanion WoweeCompanionLoader::makeRare(
    const std::string& catalogName) {
    WoweeCompanion c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t creature, const char* name,
                    uint32_t spellId, uint32_t itemId,
                    uint8_t kind, uint8_t rarity, const char* desc) {
        WoweeCompanion::Entry e;
        e.companionId = id; e.creatureId = creature;
        e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_Pet_") +
                      name + ".blp";
        e.companionKind = kind;
        e.rarity = rarity;
        e.learnSpellId = spellId;
        e.itemId = itemId;
        c.entries.push_back(e);
    };
    add(100, 11325, "MiniDiablo",   23147, 18639,
        WoweeCompanion::Demonic, WoweeCompanion::Epic,
        "Promotional Diablo II tie-in pet.");
    add(101, 11326, "PandaCub",     23163, 18646,
        WoweeCompanion::Critter, WoweeCompanion::Epic,
        "Collector's edition panda cub.");
    add(102, 11327, "Zergling",     23161, 18647,
        WoweeCompanion::Critter, WoweeCompanion::Epic,
        "Promotional StarCraft tie-in pet.");
    add(103, 16599, "Murky",        25746, 21337,
        WoweeCompanion::Critter, WoweeCompanion::Epic,
        "Blizzcon 2005 promotional baby murloc.");
    return c;
}

WoweeCompanion WoweeCompanionLoader::makeFaction(
    const std::string& catalogName) {
    WoweeCompanion c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t creature, const char* name,
                    uint32_t spellId, uint32_t itemId,
                    uint8_t kind, uint8_t faction, const char* desc) {
        WoweeCompanion::Entry e;
        e.companionId = id; e.creatureId = creature;
        e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_Pet_") +
                      name + ".blp";
        e.companionKind = kind;
        e.rarity = WoweeCompanion::Uncommon;
        e.factionRestriction = faction;
        e.learnSpellId = spellId;
        e.itemId = itemId;
        c.entries.push_back(e);
    };
    add(200, 17254, "AllianceLionCub",   29726, 23713,
        WoweeCompanion::Critter, WoweeCompanion::AllianceOnly,
        "Stormwind orphan-week reward - Alliance only.");
    add(201, 17255, "HordeMottledBoar",  29727, 23714,
        WoweeCompanion::Critter, WoweeCompanion::HordeOnly,
        "Orgrimmar orphan-week reward - Horde only.");
    add(202, 33272, "ArgentSquire",      54187, 39286,
        WoweeCompanion::Critter, WoweeCompanion::AnyFaction,
        "Argent Tournament squire - any faction may purchase.");
    return c;
}

} // namespace pipeline
} // namespace wowee
