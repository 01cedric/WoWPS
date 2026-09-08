#include "pipeline/wowee_bags.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'N', 'K'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wbnk";

} // namespace

const WoweeBagSlot::Entry*
WoweeBagSlot::findById(uint32_t bagSlotId) const {
    for (const auto& e : entries)
        if (e.bagSlotId == bagSlotId) return &e;
    return nullptr;
}

const char* WoweeBagSlot::bagKindName(uint8_t k) {
    switch (k) {
        case Inventory: return "inventory";
        case Bank:      return "bank";
        case Keyring:   return "keyring";
        case Quiver:    return "quiver";
        case SoulShard: return "soul-shard";
        case Stable:    return "stable";
        case Reagent:   return "reagent";
        case Wallet:    return "wallet";
        default:        return "unknown";
    }
}

bool WoweeBagSlotLoader::save(const WoweeBagSlot& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeBagSlot::Entry& e) {
        writePOD(os, e.bagSlotId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.bagKind);
        writePOD(os, e.containerSize);
        writePOD(os, e.displayOrder);
        writePOD(os, e.isUnlocked);
        writePOD(os, e.fixedBagItemId);
        writePOD(os, e.unlockCostCopper);
        writePOD(os, e.acceptsBagSubclassMask);
                       });
}

WoweeBagSlot WoweeBagSlotLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeBagSlot>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeBagSlot::Entry& e) {
        if (!readPOD(is, e.bagSlotId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.bagKind) ||
            !readPOD(is, e.containerSize) ||
            !readPOD(is, e.displayOrder) ||
            !readPOD(is, e.isUnlocked) ||
            !readPOD(is, e.fixedBagItemId) ||
            !readPOD(is, e.unlockCostCopper) ||
            !readPOD(is, e.acceptsBagSubclassMask)) { return false; }
                                  return true;
                              });
}

bool WoweeBagSlotLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeBagSlot WoweeBagSlotLoader::makeStarter(
    const std::string& catalogName) {
    WoweeBagSlot c;
    c.name = catalogName;
    {
        // Main backpack - 16-slot fixed, item id 0 = built-in.
        WoweeBagSlot::Entry e;
        e.bagSlotId = 1;
        e.name = "MainBackpack";
        e.description = "Built-in 16-slot starter backpack - "
                         "always present, never empty.";
        e.bagKind = WoweeBagSlot::Inventory;
        e.containerSize = 16;
        e.displayOrder = 0;
        e.isUnlocked = 1;
        e.acceptsBagSubclassMask = 0;   // no equippable bag here
        c.entries.push_back(e);
    }
    auto addBagSlot = [&](uint32_t id, uint8_t order) {
        WoweeBagSlot::Entry e;
        e.bagSlotId = id;
        e.name = std::string("BagSlot") + std::to_string(order);
        e.description = std::string("Player-equippable bag slot ") +
                         std::to_string(order) +
                         " - accepts any generic container.";
        e.bagKind = WoweeBagSlot::Inventory;
        e.containerSize = 0;             // size determined by equipped bag
        e.displayOrder = order;
        e.isUnlocked = 1;
        e.acceptsBagSubclassMask =
            WoweeBagSlot::kAcceptsAnyContainer |
            WoweeBagSlot::kAcceptsHerb |
            WoweeBagSlot::kAcceptsEnchanting;
        c.entries.push_back(e);
    };
    addBagSlot(2, 1);
    addBagSlot(3, 2);
    addBagSlot(4, 3);
    addBagSlot(5, 4);
    return c;
}

WoweeBagSlot WoweeBagSlotLoader::makeBank(
    const std::string& catalogName) {
    WoweeBagSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint8_t order, uint8_t unlocked,
                    uint32_t cost) {
        WoweeBagSlot::Entry e;
        e.bagSlotId = id;
        e.name = std::string("BankBag") + std::to_string(order);
        e.description = std::string("Bank bag slot ") +
                         std::to_string(order) +
                         (unlocked
                            ? " - free, unlocked at character creation."
                            : " - requires gold purchase to unlock.");
        e.bagKind = WoweeBagSlot::Bank;
        e.containerSize = 0;
        e.displayOrder = order;
        e.isUnlocked = unlocked;
        e.unlockCostCopper = cost;
        e.acceptsBagSubclassMask =
            WoweeBagSlot::kAcceptsAnyContainer |
            WoweeBagSlot::kAcceptsHerb |
            WoweeBagSlot::kAcceptsEnchanting |
            WoweeBagSlot::kAcceptsEngineer |
            WoweeBagSlot::kAcceptsGem |
            WoweeBagSlot::kAcceptsMining |
            WoweeBagSlot::kAcceptsLeather |
            WoweeBagSlot::kAcceptsInscription;
        c.entries.push_back(e);
    };
    // Bank bag costs match canonical WoW bank bag costs:
    // slots 0+1 are free, then 10s, 1g, 10g, 25g, 50g, 100g.
    // 1g = 10000c; 1s = 100c.
    add(100, 0, 1, 0);
    add(101, 1, 1, 0);
    add(102, 2, 0,    1000);     // 10 silver
    add(103, 3, 0,   10000);     //  1 gold
    add(104, 4, 0,  100000);     // 10 gold
    add(105, 5, 0,  250000);     // 25 gold
    add(106, 6, 0,  500000);     // 50 gold
    add(107, 7, 0, 1000000);     // 100 gold
    return c;
}

WoweeBagSlot WoweeBagSlotLoader::makeSpecial(
    const std::string& catalogName) {
    WoweeBagSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t size, uint32_t mask, const char* desc) {
        WoweeBagSlot::Entry e;
        e.bagSlotId = id; e.name = name; e.description = desc;
        e.bagKind = kind;
        e.containerSize = size;
        e.acceptsBagSubclassMask = mask;
        c.entries.push_back(e);
    };
    add(200, "Keyring",        WoweeBagSlot::Keyring,   32, 0,
        "Fixed 32-slot keyring - accepts only key-class items "
        "(no equippable bag).");
    add(201, "SoulShardBag",   WoweeBagSlot::SoulShard, 0,
        WoweeBagSlot::kAcceptsSoulShard,
        "Warlock-only soul shard bag slot - accepts only "
        "Soul Shard Bag containers.");
    add(202, "ArrowQuiver",    WoweeBagSlot::Quiver,    0,
        WoweeBagSlot::kAcceptsQuiver | WoweeBagSlot::kAcceptsAmmoPouch,
        "Hunter-only ranged ammo slot - accepts quivers and "
        "ammo pouches (boost ranged attack speed).");
    add(203, "HuntersStable",  WoweeBagSlot::Stable,    5, 0,
        "5 hunter pet stable slots - only hunters can use this.");
    return c;
}

} // namespace pipeline
} // namespace wowee
