#include "pipeline/wowee_creature_equipment.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'E', 'Q'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wceq";

} // namespace

const WoweeCreatureEquipment::Entry*
WoweeCreatureEquipment::findById(uint32_t equipmentId) const {
    for (const auto& e : entries)
        if (e.equipmentId == equipmentId) return &e;
    return nullptr;
}

bool WoweeCreatureEquipmentLoader::save(
    const WoweeCreatureEquipment& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.equipmentId);
        writePOD(os, e.creatureId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mainHandItemId);
        writePOD(os, e.offHandItemId);
        writePOD(os, e.rangedItemId);
        writePOD(os, e.mainHandSlot);
        writePOD(os, e.offHandSlot);
        writePOD(os, e.rangedSlot);
        writePOD(os, e.equipFlags);
        writePOD(os, e.mainHandVisualId);
    });
}

WoweeCreatureEquipment WoweeCreatureEquipmentLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCreatureEquipment>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCreatureEquipment::Entry& e) {
        if (!readPOD(is, e.equipmentId) ||
            !readPOD(is, e.creatureId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.mainHandItemId) ||
            !readPOD(is, e.offHandItemId) ||
            !readPOD(is, e.rangedItemId) ||
            !readPOD(is, e.mainHandSlot) ||
            !readPOD(is, e.offHandSlot) ||
            !readPOD(is, e.rangedSlot) ||
            !readPOD(is, e.equipFlags) ||
            !readPOD(is, e.mainHandVisualId)) { return false; }
                                  return true;
                              });
}

bool WoweeCreatureEquipmentLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeCreatureEquipment WoweeCreatureEquipmentLoader::makeStarter(
    const std::string& catalogName) {
    WoweeCreatureEquipment c;
    c.name = catalogName;
    {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = 1; e.creatureId = 295;   // Stormwind Guard
        e.name = "GuardSwordAndShield";
        e.description = "Stormwind guard - 1H sword + heater shield.";
        e.mainHandItemId = 1909;     // Long Sword
        e.offHandItemId = 2129;      // Heater Shield
        e.equipFlags = WoweeCreatureEquipment::kFlagShieldOffhand;
        c.entries.push_back(e);
    }
    {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = 2; e.creatureId = 1419;  // Hunter trainer
        e.name = "HunterBowAndOffhand";
        e.description = "Bow with quiver - no weapon in offhand.";
        e.mainHandItemId = 2511;     // Worn Shortsword
        e.rangedItemId = 2504;       // Worn Shortbow
        c.entries.push_back(e);
    }
    {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = 3; e.creatureId = 1416;  // Rogue trainer
        e.name = "RogueDualDagger";
        e.description = "Dual-wielding daggers (mainhand + offhand).";
        e.mainHandItemId = 2092;     // Worn Dagger
        e.offHandItemId = 2092;
        e.equipFlags = WoweeCreatureEquipment::kFlagDualWield;
        c.entries.push_back(e);
    }
    return c;
}

WoweeCreatureEquipment WoweeCreatureEquipmentLoader::makeBosses(
    const std::string& catalogName) {
    WoweeCreatureEquipment c;
    c.name = catalogName;
    auto add = [&](uint32_t eid, uint32_t cid, const char* name,
                    uint32_t mainHand, uint32_t offHand,
                    uint32_t ranged, uint8_t flags,
                    uint32_t visualKitId, const char* desc) {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = eid; e.creatureId = cid;
        e.name = name; e.description = desc;
        e.mainHandItemId = mainHand;
        e.offHandItemId = offHand;
        e.rangedItemId = ranged;
        e.equipFlags = flags;
        e.mainHandVisualId = visualKitId;   // WSVK cross-ref
        c.entries.push_back(e);
    };
    // Iconic boss loadouts. visualKitId values reference WSVK
    // entries - non-zero so the brandished weapon plays its
    // signature glow / aura.
    add(100, 11583, "Onyxian2HSword", 17075, 0, 0,
        WoweeCreatureEquipment::kFlagPolearmTwoHand, 100,
        "Onyxia's caster form - 2H greatsword with smoke trail.");
    add(101, 36597, "FrostmournePrime", 49623, 0, 0,
        WoweeCreatureEquipment::kFlagPolearmTwoHand, 101,
        "The Lich King - Frostmourne with frost runes.");
    add(102, 37955, "SylvanasBow", 0, 0, 50613,
        0, 102,
        "Sylvanas Windrunner - bow only, banshee aura.");
    add(103, 22917, "IllidanDualWarglaives", 32837, 32837, 0,
        WoweeCreatureEquipment::kFlagDualWield, 103,
        "Illidan Stormrage - dual warglaives, fel green glow.");
    return c;
}

WoweeCreatureEquipment WoweeCreatureEquipmentLoader::makeRanged(
    const std::string& catalogName) {
    WoweeCreatureEquipment c;
    c.name = catalogName;
    auto add = [&](uint32_t eid, uint32_t cid, const char* name,
                    uint32_t rangedItem, uint8_t flags,
                    const char* desc) {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = eid; e.creatureId = cid;
        e.name = name; e.description = desc;
        e.rangedItemId = rangedItem;
        e.equipFlags = flags;
        c.entries.push_back(e);
    };
    add(200, 829,  "DwarfRifleman",   2511, 0,
        "Dwarf rifleman - long rifle in ranged slot only.");
    add(201, 1419, "ElvenLongbow",    2504, 0,
        "Night-elf hunter - long bow in ranged slot.");
    add(202, 6791, "TrollCrossbow",   2508, 0,
        "Troll Sentinel - crossbow in ranged slot.");
    return c;
}

} // namespace pipeline
} // namespace wowee
