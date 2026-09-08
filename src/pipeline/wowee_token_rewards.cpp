#include "pipeline/wowee_token_rewards.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'B', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wtbr";

} // namespace

const WoweeTokenReward::Entry*
WoweeTokenReward::findById(uint32_t tokenRewardId) const {
    for (const auto& e : entries)
        if (e.tokenRewardId == tokenRewardId) return &e;
    return nullptr;
}

std::vector<const WoweeTokenReward::Entry*>
WoweeTokenReward::findByToken(uint32_t tokenItemId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.spentTokenItemId == tokenItemId) out.push_back(&e);
    }
    return out;
}

const char* WoweeTokenReward::rewardKindName(uint8_t k) {
    switch (k) {
        case Item:     return "item";
        case Spell:    return "spell";
        case Title:    return "title";
        case Mount:    return "mount";
        case Pet:      return "pet";
        case Currency: return "currency";
        case Heirloom: return "heirloom";
        case Cosmetic: return "cosmetic";
        default:       return "unknown";
    }
}

const char* WoweeTokenReward::factionStandingName(uint8_t s) {
    switch (s) {
        case Hated:      return "hated";
        case Hostile:    return "hostile";
        case Unfriendly: return "unfriendly";
        case Neutral:    return "neutral";
        case Friendly:   return "friendly";
        case Honored:    return "honored";
        case Revered:    return "revered";
        case Exalted:    return "exalted";
        default:         return "unknown";
    }
}

bool WoweeTokenRewardLoader::save(const WoweeTokenReward& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeTokenReward::Entry& e) {
        writePOD(os, e.tokenRewardId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.spentTokenItemId);
        writePOD(os, e.spentTokenCount);
        writePOD(os, e.rewardKind);
        writePOD(os, e.requiredFactionStanding);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.rewardId);
        writePOD(os, e.rewardCount);
        writePOD(os, e.requiredFactionId);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeTokenReward WoweeTokenRewardLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeTokenReward>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeTokenReward::Entry& e) {
        if (!readPOD(is, e.tokenRewardId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.spentTokenItemId) ||
            !readPOD(is, e.spentTokenCount) ||
            !readPOD(is, e.rewardKind) ||
            !readPOD(is, e.requiredFactionStanding) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.rewardId) ||
            !readPOD(is, e.rewardCount) ||
            !readPOD(is, e.requiredFactionId) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeTokenRewardLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeTokenReward WoweeTokenRewardLoader::makeRaidTokens(
    const std::string& catalogName) {
    using T = WoweeTokenReward;
    WoweeTokenReward c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t tokenItem,
                    uint32_t tokenCount, uint32_t rewardItem,
                    const char* desc) {
        T::Entry e;
        e.tokenRewardId = id; e.name = name; e.description = desc;
        e.spentTokenItemId = tokenItem;
        e.spentTokenCount = tokenCount;
        e.rewardKind = T::Item;
        e.rewardId = rewardItem;
        e.rewardCount = 1;
        e.iconColorRGBA = packRgba(180, 100, 240);   // raid epic purple
        c.entries.push_back(e);
    };
    // Item 47241 = Trophy of the Crusade (T9 redeem token).
    // Item 49426 = Emblem of Frost (T10 redeem token).
    // Item ids on the right are illustrative tier-set helms.
    add(1, "T9_Conqueror_Helm",  47241, 1, 47242,
        "T9 Conqueror's Helm - 1 Trophy of the Crusade.");
    add(2, "T9_Vanquisher_Chest",47241, 1, 47243,
        "T9 Vanquisher's Chest - 1 Trophy of the Crusade.");
    add(3, "T10_Protector_Legs", 49426, 95, 50688,
        "T10 Protector's Greaves - 95 Emblem of Frost.");
    add(4, "T10_Trophy_Gloves",  49426, 60, 50689,
        "T10 Trophy Gauntlets - 60 Emblem of Frost.");
    add(5, "T10_Helm_Crusader",  49426, 95, 50690,
        "T10 Crusader's Helm - 95 Emblem of Frost.");
    return c;
}

WoweeTokenReward WoweeTokenRewardLoader::makePvP(
    const std::string& catalogName) {
    using T = WoweeTokenReward;
    WoweeTokenReward c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t tokenItem,
                    uint32_t tokenCount, uint8_t rewardKind,
                    uint32_t rewardId, const char* desc) {
        T::Entry e;
        e.tokenRewardId = id; e.name = name; e.description = desc;
        e.spentTokenItemId = tokenItem;
        e.spentTokenCount = tokenCount;
        e.rewardKind = rewardKind;
        e.rewardId = rewardId;
        e.rewardCount = 1;
        e.iconColorRGBA = packRgba(220, 80, 100);    // pvp red
        c.entries.push_back(e);
    };
    // Honor Points = 43308; Arena Points = 29024 (item form).
    add(100, "PvPMount_Stallion",     43308, 50000,  T::Mount,
        4806,  "PvP Stallion - 50000 honor points.");
    add(101, "ArenaWeapon_Sword",     29024,  3650, T::Item,
        51811, "Arena Season 8 Sword - 3650 Arena Points.");
    add(102, "PvPHelm_Wrathful",      29024,  1650, T::Item,
        51812, "Wrathful Gladiator's Helm - 1650 Arena Points.");
    add(103, "ConquestTitle_Combatant",29024,    50, T::Title,
        38,    "Combatant title - 50 Arena Points.");
    add(104, "PvPTabard",             43308,  3000, T::Cosmetic,
        45984, "PvP Tabard - 3000 honor points.");
    return c;
}

WoweeTokenReward WoweeTokenRewardLoader::makeFaction(
    const std::string& catalogName) {
    using T = WoweeTokenReward;
    WoweeTokenReward c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t tokenItem,
                    uint32_t tokenCount, uint8_t rewardKind,
                    uint32_t rewardId, uint32_t factionId,
                    uint8_t standing, const char* desc) {
        T::Entry e;
        e.tokenRewardId = id; e.name = name; e.description = desc;
        e.spentTokenItemId = tokenItem;
        e.spentTokenCount = tokenCount;
        e.rewardKind = rewardKind;
        e.rewardId = rewardId;
        e.rewardCount = 1;
        e.requiredFactionId = factionId;
        e.requiredFactionStanding = standing;
        e.iconColorRGBA = packRgba(100, 200, 100);   // faction green
        c.entries.push_back(e);
    };
    // Token items: Champion's Seal (44990, Argent Tournament),
    // Spear-fragment of Hodir (41511, Sons of Hodir).
    add(200, "ArgentTabard",  44990, 25, T::Cosmetic, 45984, 1106,  T::Honored,
        "Argent Tournament tabard - 25 Champion's Seals "
        "@ Honored with Argent Crusade.");
    add(201, "HodirMammoth",  41511,200, T::Mount,    44171, 1119,  T::Exalted,
        "Sons of Hodir Mammoth - 200 Spear-fragments "
        "@ Exalted with Sons of Hodir.");
    add(202, "CenarionRing",  20809,150, T::Item,     19438,  609,  T::Revered,
        "Cenarion Ring of Casting - 150 Marks of Cenarion "
        "@ Revered with Cenarion Circle.");
    add(203, "ArgentTitle",   44990,250, T::Title,    149,   1106,  T::Exalted,
        "Crusader title - 250 Champion's Seals @ Exalted "
        "with Argent Crusade.");
    add(204, "WintergraspPet",43589, 30, T::Pet,      45890, 1156,  T::Honored,
        "Wintergrasp commemorative pet - 30 Wintergrasp "
        "Marks @ Honored with The Wintergrasp Defenders.");
    return c;
}

} // namespace pipeline
} // namespace wowee
