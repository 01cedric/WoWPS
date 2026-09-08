#include "pipeline/wowee_trade_rules.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'R', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wtrd";

} // namespace

const WoweeTradeRules::Entry*
WoweeTradeRules::findById(uint32_t ruleId) const {
    for (const auto& e : entries)
        if (e.ruleId == ruleId) return &e;
    return nullptr;
}

std::vector<const WoweeTradeRules::Entry*>
WoweeTradeRules::findByKind(uint8_t ruleKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.ruleKind == ruleKind) out.push_back(&e);
    return out;
}

bool WoweeTradeRulesLoader::save(const WoweeTradeRules& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeTradeRules::Entry& e) {
        writePOD(os, e.ruleId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.ruleKind);
        writePOD(os, e.targetingFilter);
        writePOD(os, e.levelRequirement);
        writePOD(os, e.priority);
        writePOD(os, e.itemCategoryFilter);
        writePOD(os, e.goldEscrowMaxCopper);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeTradeRules WoweeTradeRulesLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeTradeRules>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeTradeRules::Entry& e) {
        if (!readPOD(is, e.ruleId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.ruleKind) ||
            !readPOD(is, e.targetingFilter) ||
            !readPOD(is, e.levelRequirement) ||
            !readPOD(is, e.priority) ||
            !readPOD(is, e.itemCategoryFilter) ||
            !readPOD(is, e.goldEscrowMaxCopper) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeTradeRulesLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeTradeRules WoweeTradeRulesLoader::makeStandard(
    const std::string& catalogName) {
    using T = WoweeTradeRules;
    WoweeTradeRules c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, uint8_t targeting,
                    uint8_t levelReq, uint8_t prio,
                    uint32_t catFilter,
                    uint64_t goldMax, const char* desc) {
        T::Entry e;
        e.ruleId = id; e.name = name; e.description = desc;
        e.ruleKind = kind;
        e.targetingFilter = targeting;
        e.levelRequirement = levelReq;
        e.priority = prio;
        e.itemCategoryFilter = catFilter;
        e.goldEscrowMaxCopper = goldMax;
        e.iconColorRGBA = packRgba(140, 200, 255);   // standard blue
        c.entries.push_back(e);
    };
    // itemClass bits: 0=Consumable, 1=Container, 2=Weapon,
    // 3=Gem, 4=Armor, 5=Reagent, 7=Projectile, 9=Recipe,
    // 12=Quest, 15=Misc.
    add(1, "SoulboundForbidden", T::Forbidden, T::AnyPlayer,
        0, 10, 0xFFFFFFFFu, 0,
        "Globally forbid trading soulbound items "
        "(itemCategoryFilter=0xFFFFFFFF means all "
        "categories). Priority 10 - base default rule.");
    add(2, "QuestItemForbidden", T::Forbidden, T::AnyPlayer,
        0, 10, 1u << 12, 0,
        "Forbid quest items (itemClass=12). Priority "
        "10 - base default. Quest items are inventory-"
        "frozen by design.");
    add(3, "RaidTradeBackException", T::SoulboundException,
        T::SameRealmOnly,
        0, 20, 0, 0,
        "2-hour trade-back window for raid loot - "
        "overrides the SoulboundForbidden rule when "
        "the soulbind happened within 2hr to allow "
        "loot redistribution to absent players. "
        "Priority 20 - overrides rule 1.");
    add(4, "SameFactionOnly", T::Forbidden, T::SameFactionOnly,
        0, 5, 0, 0,
        "Default cross-faction trade restriction - "
        "Alliance and Horde players cannot initiate "
        "trades. Priority 5 - low so server-custom "
        "CrossFactionAllowed can override.");
    return c;
}

WoweeTradeRules WoweeTradeRulesLoader::makeServerAdmin(
    const std::string& catalogName) {
    using T = WoweeTradeRules;
    WoweeTradeRules c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, uint8_t targeting,
                    uint8_t levelReq, uint8_t prio,
                    uint64_t goldMax, const char* desc) {
        T::Entry e;
        e.ruleId = id; e.name = name; e.description = desc;
        e.ruleKind = kind;
        e.targetingFilter = targeting;
        e.levelRequirement = levelReq;
        e.priority = prio;
        e.itemCategoryFilter = 0;
        e.goldEscrowMaxCopper = goldMax;
        e.iconColorRGBA = packRgba(220, 60, 60);   // GM red
        c.entries.push_back(e);
    };
    add(100, "GMEscrowTrade", T::Allowed, T::GMOnly,
        0, 100, 0,
        "GM-only trade with no item/gold restriction "
        "for staff-mediated player disputes. Priority "
        "100 - overrides all other rules.");
    add(101, "AccountBoundOwnTransfer", T::Allowed,
        T::SameAccountOnly,
        0, 90, 0,
        "Allow trading account-bound items between own "
        "characters via a cross-realm trade window. "
        "Priority 90 - overrides Soulbound default.");
    add(102, "CrossFactionAt80", T::CrossFactionAllowed,
        T::AnyPlayer,
        80, 50, 0,
        "Allow cross-faction trades at level 80+ for "
        "server-custom RP servers. Overrides the "
        "default SameFactionOnly Forbidden rule for "
        "max-level players. Priority 50.");
    return c;
}

WoweeTradeRules WoweeTradeRulesLoader::makeRMTPrevent(
    const std::string& catalogName) {
    using T = WoweeTradeRules;
    WoweeTradeRules c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, uint8_t targeting,
                    uint8_t levelReq, uint8_t prio,
                    uint64_t goldMax, const char* desc) {
        T::Entry e;
        e.ruleId = id; e.name = name; e.description = desc;
        e.ruleKind = kind;
        e.targetingFilter = targeting;
        e.levelRequirement = levelReq;
        e.priority = prio;
        e.itemCategoryFilter = 0;
        e.goldEscrowMaxCopper = goldMax;
        e.iconColorRGBA = packRgba(220, 200, 80);   // anti-RMT yellow
        c.entries.push_back(e);
    };
    // costCopper: 1g = 10000 copper, 1000g = 10,000,000.
    add(200, "LowLevelGoldCap", T::GoldEscrowMax,
        T::AnyPlayer,
        0, 30, 100000,
        "Cap gold side at 10g for level <30 trades - "
        "anti-RMT (gold-buying typically targets fresh "
        "accounts). Priority 30. levelRequirement=0 "
        "but the rule is meant to apply to LOW levels; "
        "the trade engine inverts this check at runtime.");
    add(201, "NewAccountValueCap", T::GoldEscrowMax,
        T::AnyPlayer,
        30, 25, 5000000,
        "Cap gold side at 500g for level 30+ trades on "
        "accounts < 30 days old. Stops mid-tier RMT "
        "transfers. Priority 25.");
    add(202, "HighValueAuditLog", T::AuditLogged,
        T::AnyPlayer,
        0, 15, 10000000,
        "Log all trades with gold side > 1000g for "
        "audit review. Doesn't block; just records. "
        "Priority 15. Server admins can run "
        "anti-RMT analytics on the log.");
    add(203, "FirstTradeMandatoryDelay", T::Forbidden,
        T::AnyPlayer,
        0, 10, 0,
        "Block first trade for accounts < 24hr old. "
        "Manual placeholder rule - the trade engine "
        "enforces the time check externally. "
        "Priority 10.");
    return c;
}

} // namespace pipeline
} // namespace wowee
