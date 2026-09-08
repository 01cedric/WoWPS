#include "pipeline/wowee_soulbind_rules.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'N', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wbnd";

} // namespace

const WoweeSoulbindRules::Entry*
WoweeSoulbindRules::findById(uint32_t ruleId) const {
    for (const auto& e : entries)
        if (e.ruleId == ruleId) return &e;
    return nullptr;
}

const WoweeSoulbindRules::Entry*
WoweeSoulbindRules::resolveForQuality(uint8_t itemQuality) const {
    // Walk all entries; pick the rule with highest
    // qualityFloor that is <= itemQuality. Ties
    // broken by ruleId (lower wins for stable
    // resolution).
    const Entry* best = nullptr;
    for (const auto& e : entries) {
        if (e.itemQualityFloor > itemQuality) continue;
        if (!best ||
            e.itemQualityFloor > best->itemQualityFloor ||
            (e.itemQualityFloor == best->itemQualityFloor &&
             e.ruleId < best->ruleId)) {
            best = &e;
        }
    }
    return best;
}

std::vector<const WoweeSoulbindRules::Entry*>
WoweeSoulbindRules::findByBindKind(uint8_t bindKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.bindKind == bindKind) out.push_back(&e);
    return out;
}

bool WoweeSoulbindRulesLoader::save(const WoweeSoulbindRules& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSoulbindRules::Entry& e) {
        writePOD(os, e.ruleId);
        writeStr(os, e.name);
        writePOD(os, e.bindKind);
        writePOD(os, e.itemQualityFloor);
        writePOD(os, e.tradableForRaidGroup);
        writePOD(os, e.boeBecomesBoP);
        writePOD(os, e.accountBoundCrossFaction);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.tradableWindowSec);
        writeStr(os, e.description);
                       });
}

WoweeSoulbindRules WoweeSoulbindRulesLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSoulbindRules>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSoulbindRules::Entry& e) {
        if (!readPOD(is, e.ruleId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.bindKind) ||
            !readPOD(is, e.itemQualityFloor) ||
            !readPOD(is, e.tradableForRaidGroup) ||
            !readPOD(is, e.boeBecomesBoP) ||
            !readPOD(is, e.accountBoundCrossFaction) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.tradableWindowSec)) { return false; }
        if (!readStr(is, e.description)) { return false; }
                                  return true;
                              });
}

bool WoweeSoulbindRulesLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeSoulbindRules::Entry makeRule(
    uint32_t ruleId, const char* name,
    uint8_t bindKind, uint8_t qualityFloor,
    uint8_t raidTrade, uint8_t boeToBoP,
    uint8_t crossFaction,
    uint32_t windowSec, const char* desc) {
    WoweeSoulbindRules::Entry e;
    e.ruleId = ruleId; e.name = name;
    e.bindKind = bindKind;
    e.itemQualityFloor = qualityFloor;
    e.tradableForRaidGroup = raidTrade;
    e.boeBecomesBoP = boeToBoP;
    e.accountBoundCrossFaction = crossFaction;
    e.tradableWindowSec = windowSec;
    e.description = desc;
    return e;
}

} // namespace

WoweeSoulbindRules WoweeSoulbindRulesLoader::makeVanillaPolicy(
    const std::string& catalogName) {
    using B = WoweeSoulbindRules;
    WoweeSoulbindRules c;
    c.name = catalogName;
    // Vanilla 1.12: no raid-trade window. Items bind
    // INSTANTLY on pickup with no second chance.
    c.entries.push_back(makeRule(
        1, "Vanilla Poor (gray vendor trash)",
        B::NoBind, B::Poor, 0, 0, 0, 0,
        "Gray-quality items never bind - always "
        "tradable / vendorable. Used for vendor trash "
        "and crafting reagents."));
    c.entries.push_back(makeRule(
        2, "Vanilla Common (white)",
        B::BindOnEquip, B::Common, 0, 0, 0, 0,
        "White-quality items bind on equip. Once "
        "equipped, bound to character permanently."));
    c.entries.push_back(makeRule(
        3, "Vanilla Uncommon (green) and above",
        B::BindOnPickup, B::Uncommon, 0, 0, 0, 0,
        "Green+ quality items bind on pickup. NO "
        "trade window in vanilla - pick it up, it's "
        "yours forever. Famous source of master-loot "
        "drama."));
    c.entries.push_back(makeRule(
        4, "Vanilla Epic+ (purple/orange)",
        B::Soulbound, B::Epic, 0, 0, 0, 0,
        "Epic+ quality items arrive already "
        "Soulbound at loot - no transfer possible "
        "even before pickup acknowledgement."));
    return c;
}

WoweeSoulbindRules WoweeSoulbindRulesLoader::makeTBCPolicy(
    const std::string& catalogName) {
    using B = WoweeSoulbindRules;
    WoweeSoulbindRules c;
    c.name = catalogName;
    // TBC 2.4.3 added the 2-hour raid-trade window
    // for BoP items. Players who looted a piece had
    // 7200s to trade it to a raid member who was
    // present at the kill.
    c.entries.push_back(makeRule(
        10, "TBC Poor (gray vendor trash)",
        B::NoBind, B::Poor, 0, 0, 0, 0,
        "Gray-quality items never bind."));
    c.entries.push_back(makeRule(
        11, "TBC Common (white)",
        B::BindOnEquip, B::Common, 0, 0, 0, 0,
        "BoE - bind on equip."));
    c.entries.push_back(makeRule(
        12, "TBC Uncommon (green)",
        B::BindOnPickup, B::Uncommon, 1, 0, 0, 7200,
        "BoP with 2hr raid-trade window. Looter can "
        "transfer to a raid member present at the "
        "kill within 2hr."));
    c.entries.push_back(makeRule(
        13, "TBC Rare (blue)",
        B::BindOnPickup, B::Rare, 1, 0, 0, 7200,
        "BoP with 2hr raid-trade window."));
    c.entries.push_back(makeRule(
        14, "TBC Epic+ (purple/orange)",
        B::Soulbound, B::Epic, 0, 0, 0, 0,
        "Epic+ items arrive Soulbound - no trade "
        "even within window."));
    return c;
}

WoweeSoulbindRules WoweeSoulbindRulesLoader::makeWotLKPolicy(
    const std::string& catalogName) {
    using B = WoweeSoulbindRules;
    WoweeSoulbindRules c;
    c.name = catalogName;
    // WotLK 3.3.5a kept TBC's raid-trade window and
    // ADDED Heirloom (Account-Bound, cross-faction
    // for the 80-twink path).
    c.entries.push_back(makeRule(
        20, "WotLK Poor (gray)",
        B::NoBind, B::Poor, 0, 0, 0, 0,
        "Never binds."));
    c.entries.push_back(makeRule(
        21, "WotLK Common (white)",
        B::BindOnEquip, B::Common, 0, 0, 0, 0,
        "BoE."));
    c.entries.push_back(makeRule(
        22, "WotLK Uncommon (green)",
        B::BindOnPickup, B::Uncommon, 1, 0, 0, 7200,
        "BoP with 2hr raid-trade window."));
    c.entries.push_back(makeRule(
        23, "WotLK Rare (blue)",
        B::BindOnPickup, B::Rare, 1, 0, 0, 7200,
        "BoP with 2hr raid-trade window."));
    c.entries.push_back(makeRule(
        24, "WotLK Epic+ (purple/orange)",
        B::Soulbound, B::Epic, 0, 0, 0, 0,
        "Soulbound on loot - no trade."));
    c.entries.push_back(makeRule(
        25, "WotLK Heirloom (gold)",
        B::BindOnAccount, B::Heirloom, 0, 0, 1, 0,
        "Account-bound and cross-faction. Sent via "
        "account-mail across Alliance/Horde. New in "
        "WotLK for the level-1-to-80 twink path."));
    return c;
}

} // namespace pipeline
} // namespace wowee
