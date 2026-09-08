#include "pipeline/wowee_mounts.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'O', 'U'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wmou";
constexpr uint32_t kRidingSkillId = 762;    // canonical SkillLine "Riding"

} // namespace

const WoweeMount::Entry* WoweeMount::findById(uint32_t mountId) const {
    for (const auto& e : entries) if (e.mountId == mountId) return &e;
    return nullptr;
}

const char* WoweeMount::kindName(uint8_t k) {
    switch (k) {
        case Ground:   return "ground";
        case Flying:   return "flying";
        case Swimming: return "swimming";
        case Hybrid:   return "hybrid";
        case Aquatic:  return "aquatic";
        default:       return "unknown";
    }
}

const char* WoweeMount::factionName(uint8_t f) {
    switch (f) {
        case Both:     return "both";
        case Alliance: return "alliance";
        case Horde:    return "horde";
        default:       return "unknown";
    }
}

const char* WoweeMount::categoryName(uint8_t c) {
    switch (c) {
        case Common:      return "common";
        case Epic:        return "epic";
        case Racial:      return "racial";
        case Event:       return "event";
        case Achievement: return "achievement";
        case Pvp:         return "pvp";
        case Quest:       return "quest";
        case ClassMount:  return "class";
        default:          return "unknown";
    }
}

bool WoweeMountLoader::save(const WoweeMount& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeMount::Entry& e) {
        writePOD(os, e.mountId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.displayId);
        writePOD(os, e.summonSpellId);
        writePOD(os, e.itemIdToLearn);
        writePOD(os, e.requiredSkillId);
        writePOD(os, e.requiredSkillRank);
        writePOD(os, e.speedPercent);
        writePOD(os, e.mountKind);
        writePOD(os, e.factionId);
        writePOD(os, e.categoryId);
        writePadding(os, 1);
        writePOD(os, e.raceMask);
                       });
}

WoweeMount WoweeMountLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeMount>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeMount::Entry& e) {
        if (!readPOD(is, e.mountId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.displayId) ||
            !readPOD(is, e.summonSpellId) ||
            !readPOD(is, e.itemIdToLearn) ||
            !readPOD(is, e.requiredSkillId) ||
            !readPOD(is, e.requiredSkillRank) ||
            !readPOD(is, e.speedPercent) ||
            !readPOD(is, e.mountKind) ||
            !readPOD(is, e.factionId) ||
            !readPOD(is, e.categoryId)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.raceMask)) { return false; }
                                  return true;
                              });
}

bool WoweeMountLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeMount WoweeMountLoader::makeStarter(const std::string& catalogName) {
    WoweeMount c;
    c.name = catalogName;
    {
        WoweeMount::Entry e;
        e.mountId = 1; e.name = "Brown Horse";
        e.description = "A common riding horse.";
        e.summonSpellId = 458;          // canonical Apprentice Riding mount
        e.itemIdToLearn = 5656;         // item that teaches it
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = 75;
        e.speedPercent = 60;            // +60% ground speed
        e.mountKind = WoweeMount::Ground;
        e.factionId = WoweeMount::Alliance;
        c.entries.push_back(e);
    }
    {
        WoweeMount::Entry e;
        e.mountId = 2; e.name = "Swift Gryphon";
        e.description = "Faster flying gryphon for journeyman flyers.";
        e.summonSpellId = 32242;
        e.itemIdToLearn = 25470;
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = 225;
        e.speedPercent = 280;           // +280% flying (epic flyer)
        e.mountKind = WoweeMount::Flying;
        e.factionId = WoweeMount::Alliance;
        e.categoryId = WoweeMount::Epic;
        c.entries.push_back(e);
    }
    {
        WoweeMount::Entry e;
        e.mountId = 3; e.name = "Riding Turtle";
        e.description = "A serene ambulatory turtle. Slow but steady.";
        e.summonSpellId = 30174;
        e.itemIdToLearn = 23720;
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = 75;
        e.speedPercent = 60;            // ground-level swimming-style mount
        e.mountKind = WoweeMount::Aquatic;
        c.entries.push_back(e);
    }
    return c;
}

WoweeMount WoweeMountLoader::makeRacial(const std::string& catalogName) {
    WoweeMount c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t fac,
                    uint32_t race, uint32_t spellId,
                    uint32_t itemId, uint16_t rank) {
        WoweeMount::Entry e;
        e.mountId = id; e.name = name;
        e.summonSpellId = spellId; e.itemIdToLearn = itemId;
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = rank;
        e.speedPercent = 60;
        e.mountKind = WoweeMount::Ground;
        e.factionId = fac; e.categoryId = WoweeMount::Racial;
        e.raceMask = race;
        c.entries.push_back(e);
    };
    // Alliance racial mounts.
    add(100, "Pinto", WoweeMount::Alliance, 1u << 0,  470,  2414, 75);   // Human
    add(101, "Brown Ram", WoweeMount::Alliance, 1u << 2, 6648, 5872, 75); // Dwarf
    add(102, "Striped Frostsaber", WoweeMount::Alliance, 1u << 3, 10789, 8629, 75); // NightElf
    add(103, "Grey Mechanostrider", WoweeMount::Alliance, 1u << 6, 17453, 13321, 75); // Gnome
    // Horde racial mounts.
    add(200, "Dire Wolf", WoweeMount::Horde, 1u << 1, 458, 1132, 75);    // Orc (re-uses item)
    add(201, "Skeletal Horse", WoweeMount::Horde, 1u << 4, 17463, 13332, 75); // Undead
    return c;
}

WoweeMount WoweeMountLoader::makeFlying(const std::string& catalogName) {
    WoweeMount c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint16_t speed,
                    uint16_t rankReq, uint8_t cat,
                    uint32_t spellId, uint32_t itemId) {
        WoweeMount::Entry e;
        e.mountId = id; e.name = name;
        e.summonSpellId = spellId; e.itemIdToLearn = itemId;
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = rankReq;
        e.speedPercent = speed;
        e.mountKind = WoweeMount::Flying;
        e.categoryId = cat;
        c.entries.push_back(e);
    };
    add(300, "Common Hippogryph",     60,  225, WoweeMount::Common,      32235, 25470);
    add(301, "Cenarion War Hippogryph", 100, 225, WoweeMount::Epic,        32240, 25471);
    add(302, "Bronze Drake",         280,  300, WoweeMount::Achievement, 59569, 43951);
    add(303, "Vicious War Wolf",     310,  300, WoweeMount::Pvp,         60424, 44083);
    return c;
}

} // namespace pipeline
} // namespace wowee
