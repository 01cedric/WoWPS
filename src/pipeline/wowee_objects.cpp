#include "pipeline/wowee_objects.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'O', 'T'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wgot";

} // namespace

const WoweeGameObject::Entry* WoweeGameObject::findById(uint32_t objectId) const {
    for (const auto& e : entries) {
        if (e.objectId == objectId) return &e;
    }
    return nullptr;
}

const char* WoweeGameObject::typeName(uint8_t t) {
    switch (t) {
        case Door:        return "door";
        case Button:      return "button";
        case Chest:       return "chest";
        case Container:   return "container";
        case QuestGiver:  return "quest-giver";
        case Text:        return "text";
        case Trap:        return "trap";
        case Goober:      return "goober";
        case Transport:   return "transport";
        case Mailbox:     return "mailbox";
        case MineralNode: return "ore-node";
        case HerbNode:    return "herb-node";
        case FishingNode: return "fishing-node";
        case Mount:       return "mount";
        case Sign:        return "sign";
        case Bonfire:     return "bonfire";
        default:          return "unknown";
    }
}

bool WoweeGameObjectLoader::save(const WoweeGameObject& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeGameObject::Entry& e) {
        writePOD(os, e.objectId);
        writePOD(os, e.displayId);
        writeStr(os, e.name);
        writePOD(os, e.typeId);
        writePadding(os, 3);
        writePOD(os, e.size);
        writeStr(os, e.castBarCaption);
        writePOD(os, e.requiredSkill);
        writePOD(os, e.requiredSkillValue);
        writePOD(os, e.lockId);
        writePOD(os, e.lootTableId);
        writePOD(os, e.minOpenTimeMs);
        writePOD(os, e.maxOpenTimeMs);
        writePOD(os, e.flags);
                       });
}

WoweeGameObject WoweeGameObjectLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeGameObject>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeGameObject::Entry& e) {
        if (!readPOD(is, e.objectId) ||
            !readPOD(is, e.displayId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.typeId)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.size) ||
            !readStr(is, e.castBarCaption) ||
            !readPOD(is, e.requiredSkill) ||
            !readPOD(is, e.requiredSkillValue) ||
            !readPOD(is, e.lockId) ||
            !readPOD(is, e.lootTableId) ||
            !readPOD(is, e.minOpenTimeMs) ||
            !readPOD(is, e.maxOpenTimeMs) ||
            !readPOD(is, e.flags)) { return false; }
                                  return true;
                              });
}

bool WoweeGameObjectLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeGameObject WoweeGameObjectLoader::makeStarter(const std::string& catalogName) {
    WoweeGameObject c;
    c.name = catalogName;
    {
        WoweeGameObject::Entry e;
        e.objectId = 1; e.displayId = 100;
        e.name = "Wooden Chest"; e.typeId = WoweeGameObject::Chest;
        e.castBarCaption = "Opening...";
        e.lootTableId = 1;     // matches WLOT.makeStarter creatureId
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 2; e.displayId = 110;
        e.name = "Standard Mailbox"; e.typeId = WoweeGameObject::Mailbox;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 3; e.displayId = 120;
        e.name = "Roadside Sign"; e.typeId = WoweeGameObject::Sign;
        c.entries.push_back(e);
    }
    return c;
}

WoweeGameObject WoweeGameObjectLoader::makeDungeon(const std::string& catalogName) {
    WoweeGameObject c;
    c.name = catalogName;
    {
        WoweeGameObject::Entry e;
        e.objectId = 1500; e.displayId = 130;
        e.name = "Iron Door"; e.typeId = WoweeGameObject::Door;
        e.lockId = 1;          // requires key / lockpick (future WLCK)
        e.flags = WoweeGameObject::Frozen;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 1501; e.displayId = 131;
        e.name = "Pressure Plate"; e.typeId = WoweeGameObject::Button;
        e.minOpenTimeMs = 5000; e.maxOpenTimeMs = 10000;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        // objectId = 2000 deliberately matches WLOT.makeBandit
        // creatureId-keyed loot table.
        e.objectId = 2000; e.displayId = 140;
        e.name = "Bandit Strongbox"; e.typeId = WoweeGameObject::Chest;
        e.castBarCaption = "Opening...";
        e.lootTableId = 2000;       // -> WLOT bandit chest table
        e.lockId = 2;               // light lockpick
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 1502; e.displayId = 141;
        e.name = "Boss Treasure Chest"; e.typeId = WoweeGameObject::Chest;
        e.castBarCaption = "Opening...";
        e.lootTableId = 9999;       // -> WLOT boss table
        e.size = 1.5f;              // visibly bigger than regular
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 1503; e.displayId = 150;
        e.name = "Spike Trap"; e.typeId = WoweeGameObject::Trap;
        e.minOpenTimeMs = 1500; e.maxOpenTimeMs = 1500;
        e.flags = WoweeGameObject::ScriptOnly;
        c.entries.push_back(e);
    }
    return c;
}

WoweeGameObject WoweeGameObjectLoader::makeGather(const std::string& catalogName) {
    WoweeGameObject c;
    c.name = catalogName;
    {
        WoweeGameObject::Entry e;
        e.objectId = 3000; e.displayId = 170;
        e.name = "Peacebloom"; e.typeId = WoweeGameObject::HerbNode;
        e.castBarCaption = "Herbalism";
        e.requiredSkill = 182;          // SkillLine herbalism (canonical id)
        e.requiredSkillValue = 1;
        e.flags = WoweeGameObject::Despawn;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 3001; e.displayId = 171;
        e.name = "Tin Vein"; e.typeId = WoweeGameObject::MineralNode;
        e.castBarCaption = "Mining";
        e.requiredSkill = 186;          // SkillLine mining (canonical id)
        e.requiredSkillValue = 65;
        e.flags = WoweeGameObject::Despawn;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 3002; e.displayId = 172;
        e.name = "Schools of Fish"; e.typeId = WoweeGameObject::FishingNode;
        e.castBarCaption = "Fishing";
        e.requiredSkill = 356;          // SkillLine fishing
        e.requiredSkillValue = 1;
        e.flags = WoweeGameObject::Despawn;
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
