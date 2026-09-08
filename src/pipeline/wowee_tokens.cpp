#include "pipeline/wowee_tokens.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'K', 'N'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wtkn";

} // namespace

const WoweeToken::Entry* WoweeToken::findById(uint32_t tokenId) const {
    for (const auto& e : entries) if (e.tokenId == tokenId) return &e;
    return nullptr;
}

const char* WoweeToken::categoryName(uint8_t c) {
    switch (c) {
        case Misc:       return "misc";
        case Pvp:        return "pvp";
        case Reputation: return "rep";
        case Crafting:   return "crafting";
        case Seasonal:   return "seasonal";
        case Holiday:    return "holiday";
        default:         return "unknown";
    }
}

bool WoweeTokenLoader::save(const WoweeToken& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeToken::Entry& e) {
        writePOD(os, e.tokenId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.category);
        writePadding(os, 3);
        writePOD(os, e.maxBalance);
        writePOD(os, e.weeklyCap);
        writePOD(os, e.flags);
                       });
}

WoweeToken WoweeTokenLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeToken>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeToken::Entry& e) {
        if (!readPOD(is, e.tokenId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.category)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.maxBalance) ||
            !readPOD(is, e.weeklyCap) ||
            !readPOD(is, e.flags)) { return false; }
                                  return true;
                              });
}

bool WoweeTokenLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeToken WoweeTokenLoader::makeStarter(const std::string& catalogName) {
    WoweeToken c;
    c.name = catalogName;
    {
        WoweeToken::Entry e;
        e.tokenId = 1; e.name = "Honor Points";
        e.description = "Earned by participating in PvP combat.";
        e.category = WoweeToken::Pvp;
        e.maxBalance = 75000;
        c.entries.push_back(e);
    }
    {
        WoweeToken::Entry e;
        e.tokenId = 2; e.name = "Marks of Honor";
        e.description = "Awarded for a battleground victory.";
        e.category = WoweeToken::Pvp;
        e.maxBalance = 100;
        c.entries.push_back(e);
    }
    {
        WoweeToken::Entry e;
        e.tokenId = 3; e.name = "Stormwind Guard Token";
        e.description = "Earned by serving the Stormwind militia.";
        e.category = WoweeToken::Reputation;
        e.maxBalance = 5000;
        c.entries.push_back(e);
    }
    return c;
}

WoweeToken WoweeTokenLoader::makePvp(const std::string& catalogName) {
    WoweeToken c;
    c.name = catalogName;
    {
        WoweeToken::Entry e;
        e.tokenId = 100; e.name = "Honor Points";
        e.description = "Earned in any PvP combat.";
        e.category = WoweeToken::Pvp;
        e.maxBalance = 75000;
        e.flags = WoweeToken::HiddenUntilEarned;
        c.entries.push_back(e);
    }
    {
        WoweeToken::Entry e;
        e.tokenId = 101; e.name = "Arena Points";
        e.description = "Awarded weekly based on arena rating.";
        e.category = WoweeToken::Pvp;
        e.maxBalance = 5000;
        e.weeklyCap = 1500;
        e.flags = WoweeToken::HiddenUntilEarned;
        c.entries.push_back(e);
    }
    // Marks of Honor for 6 classic battlegrounds.
    auto addMark = [&](uint32_t id, const char* bgName) {
        WoweeToken::Entry e;
        e.tokenId = id;
        e.name = std::string("Mark of Honor: ") + bgName;
        e.description =
            std::string("Awarded for a victory in ") + bgName + ".";
        e.category = WoweeToken::Pvp;
        e.maxBalance = 100;
        c.entries.push_back(e);
    };
    addMark(102, "Warsong Gulch");
    addMark(103, "Arathi Basin");
    addMark(104, "Alterac Valley");
    addMark(105, "Eye of the Storm");
    addMark(106, "Strand of the Ancients");
    addMark(107, "Isle of Conquest");
    return c;
}

WoweeToken WoweeTokenLoader::makeSeasonal(const std::string& catalogName) {
    WoweeToken c;
    c.name = catalogName;
    auto addSeasonal = [&](uint32_t id, const char* name, const char* desc) {
        WoweeToken::Entry e;
        e.tokenId = id; e.name = name; e.description = desc;
        e.category = WoweeToken::Holiday;
        e.maxBalance = 1000;
        // Seasonal tokens vanish when the event ends; flag
        // ResetsOnLogout makes the runtime drop the balance
        // on the next login if the event is no longer active.
        e.flags = WoweeToken::ResetsOnLogout;
        c.entries.push_back(e);
    };
    addSeasonal(200, "Tricky Treats",
        "Hallow's End candy currency. Spent at Headless Horseman vendors.");
    addSeasonal(201, "Brewfest Tokens",
        "Brewfest event currency from boss runs and goblin races.");
    addSeasonal(202, "Coin of Ancestry",
        "Lunar Festival elder reward; spent on holiday gear.");
    addSeasonal(203, "Stranger's Gift",
        "Winter's Veil snowball-fight reward.");
    return c;
}

} // namespace pipeline
} // namespace wowee
