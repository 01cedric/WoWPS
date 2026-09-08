#include "pipeline/wowee_reputation_rewards.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'R', 'P', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wrpr";

} // namespace

const WoweeReputationRewards::Entry*
WoweeReputationRewards::findById(uint32_t tierId) const {
    for (const auto& e : entries)
        if (e.tierId == tierId) return &e;
    return nullptr;
}

const WoweeReputationRewards::Entry*
WoweeReputationRewards::findActiveTierFor(uint32_t factionId,
                                            int32_t currentStanding) const {
    const Entry* best = nullptr;
    for (const auto& e : entries) {
        if (e.factionId != factionId) continue;
        if (currentStanding < e.minStanding) continue;
        // Highest minStanding wins.
        if (best == nullptr ||
            e.minStanding > best->minStanding) {
            best = &e;
        }
    }
    return best;
}

std::vector<const WoweeReputationRewards::Entry*>
WoweeReputationRewards::findByFaction(uint32_t factionId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.factionId == factionId) out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->minStanding < b->minStanding;
              });
    return out;
}

bool WoweeReputationRewardsLoader::save(
    const WoweeReputationRewards& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.tierId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.factionId);
        writePOD(os, e.minStanding);
        writePOD(os, e.discountPct);
        writePOD(os, e.grantsTabard);
        writePOD(os, e.grantsMount);
        writePOD(os, e.pad0);
        writePOD(os, e.iconColorRGBA);
        uint32_t itemCount = static_cast<uint32_t>(
            e.unlockedItemIds.size());
        writePOD(os, itemCount);
        for (uint32_t id : e.unlockedItemIds) writePOD(os, id);
        uint32_t recipeCount = static_cast<uint32_t>(
            e.unlockedRecipeIds.size());
        writePOD(os, recipeCount);
        for (uint32_t id : e.unlockedRecipeIds) writePOD(os, id);
    });
}

WoweeReputationRewards WoweeReputationRewardsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeReputationRewards>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeReputationRewards::Entry& e) {
        if (!readPOD(is, e.tierId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.factionId) ||
            !readPOD(is, e.minStanding) ||
            !readPOD(is, e.discountPct) ||
            !readPOD(is, e.grantsTabard) ||
            !readPOD(is, e.grantsMount) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
        uint32_t itemCount = 0;
        if (!readPOD(is, itemCount)) { return false; }
        if (itemCount > (1u << 16)) { return false; }
        e.unlockedItemIds.resize(itemCount);
        for (uint32_t k = 0; k < itemCount; ++k) {
            if (!readPOD(is, e.unlockedItemIds[k])) { return false; }
        }
        uint32_t recipeCount = 0;
        if (!readPOD(is, recipeCount)) { return false; }
        if (recipeCount > (1u << 16)) { return false; }
        e.unlockedRecipeIds.resize(recipeCount);
        for (uint32_t k = 0; k < recipeCount; ++k) {
            if (!readPOD(is, e.unlockedRecipeIds[k])) { return false; }
        }
                                  return true;
                              });
}

bool WoweeReputationRewardsLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeReputationRewards
WoweeReputationRewardsLoader::makeArgentCrusade(
    const std::string& catalogName) {
    using R = WoweeReputationRewards;
    WoweeReputationRewards c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    int32_t standing, uint8_t discount,
                    uint8_t tabard, uint8_t mount,
                    std::vector<uint32_t> items,
                    std::vector<uint32_t> recipes,
                    const char* desc) {
        R::Entry e;
        e.tierId = id; e.name = name; e.description = desc;
        e.factionId = 1106;            // Argent Crusade
        e.minStanding = standing;
        e.discountPct = discount;
        e.grantsTabard = tabard;
        e.grantsMount = mount;
        e.unlockedItemIds = std::move(items);
        e.unlockedRecipeIds = std::move(recipes);
        e.iconColorRGBA = packRgba(220, 220, 220);   // silver
        c.entries.push_back(e);
    };
    // standing thresholds: Friendly=3000, Honored=9000,
    // Revered=21000, Exalted=42000.
    add(1, "ArgentCrusade_Friendly", 3000, 0, 0, 0,
        { 44128 }, {},
        "Friendly tier - basic faction recognition. "
        "Quartermaster opens. No discount yet.");
    add(2, "ArgentCrusade_Honored", 9000, 5, 1, 0,
        { 44128, 44131, 44137 }, { 49736 },
        "Honored tier - 5%% vendor discount, tabard "
        "becomes purchasable, first crafting recipe "
        "(Argent Sword pattern) unlocks.");
    add(3, "ArgentCrusade_Revered", 21000, 10, 1, 0,
        { 44128, 44131, 44137, 44141, 44144 },
        { 49736, 49737 },
        "Revered tier - 10%% vendor discount. Two "
        "additional rare items + second recipe (Argent "
        "Plate Gauntlets) unlock.");
    add(4, "ArgentCrusade_Exalted", 42000, 15, 1, 1,
        { 44128, 44131, 44137, 44141, 44144, 44171, 44174 },
        { 49736, 49737, 49738 },
        "Exalted tier - 15%% vendor discount, the Argent "
        "Charger mount unlocks (3500g, paladin-only "
        "originally), full set of rare items, all recipes.");
    return c;
}

WoweeReputationRewards WoweeReputationRewardsLoader::makeKaluak(
    const std::string& catalogName) {
    using R = WoweeReputationRewards;
    WoweeReputationRewards c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    int32_t standing, uint8_t discount,
                    uint8_t tabard,
                    std::vector<uint32_t> items,
                    std::vector<uint32_t> recipes,
                    const char* desc) {
        R::Entry e;
        e.tierId = id; e.name = name; e.description = desc;
        e.factionId = 1073;            // The Kalu'ak
        e.minStanding = standing;
        e.discountPct = discount;
        e.grantsTabard = tabard;
        e.grantsMount = 0;
        e.unlockedItemIds = std::move(items);
        e.unlockedRecipeIds = std::move(recipes);
        e.iconColorRGBA = packRgba(140, 200, 220);   // sea blue
        c.entries.push_back(e);
    };
    add(100, "Kaluak_Friendly", 3000, 0, 0,
        { 44707 }, {},
        "Friendly - basic Kalu'ak fishing pole "
        "purchasable.");
    add(101, "Kaluak_Honored", 9000, 5, 0,
        { 44707, 44710 }, { 45550 },
        "Honored - Kalu'ak Cured Sweet Potato cooking "
        "recipe unlocks.");
    add(102, "Kaluak_Revered", 21000, 10, 1,
        { 44707, 44710, 44715 }, { 45550, 45551 },
        "Revered - Kalu'ak Tabard purchasable, second "
        "cooking recipe unlocks.");
    add(103, "Kaluak_Exalted", 42000, 15, 1,
        { 44707, 44710, 44715, 44722 },
        { 45550, 45551, 45552 },
        "Exalted - Pygmy Suit cosmetic + 3rd cooking "
        "recipe (Imperial Manta Steak) unlock. No "
        "mount reward for Kalu'ak.");
    return c;
}

WoweeReputationRewards
WoweeReputationRewardsLoader::makeAccordTabard(
    const std::string& catalogName) {
    using R = WoweeReputationRewards;
    WoweeReputationRewards c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    int32_t standing, uint8_t discount,
                    uint8_t tabard, uint8_t mount,
                    std::vector<uint32_t> items,
                    const char* desc) {
        R::Entry e;
        e.tierId = id; e.name = name; e.description = desc;
        e.factionId = 1091;            // Wyrmrest Accord
        e.minStanding = standing;
        e.discountPct = discount;
        e.grantsTabard = tabard;
        e.grantsMount = mount;
        e.unlockedItemIds = std::move(items);
        e.iconColorRGBA = packRgba(180, 60, 60);   // dragon red
        c.entries.push_back(e);
    };
    add(200, "WyrmrestAccord_Honored", 9000, 5, 0, 0,
        { 44156, 44158 },
        "Honored - first ring + cloak unlock. No tabard "
        "yet (Accord makes you wait until Revered).");
    add(201, "WyrmrestAccord_Revered", 21000, 10, 1, 0,
        { 44156, 44158, 44160 },
        "Revered - Accord Tabard purchasable + medallion. "
        "Equipping the tabard counts Wyrmrest rep on "
        "ALL Northrend Heroic kills.");
    add(202, "WyrmrestAccord_Exalted", 42000, 15, 1, 1,
        { 44156, 44158, 44160, 44178 },
        "Exalted - the Reins of the Red Drake mount "
        "unlocks (3000g). One of the iconic Wrath rep "
        "rewards.");
    return c;
}

} // namespace pipeline
} // namespace wowee
