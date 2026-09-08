#include "pipeline/wowee_chat_links.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'N', 'K'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wlnk";

} // namespace

const WoweeChatLinks::Entry*
WoweeChatLinks::findById(uint32_t linkId) const {
    for (const auto& e : entries)
        if (e.linkId == linkId) return &e;
    return nullptr;
}

const WoweeChatLinks::Entry*
WoweeChatLinks::findByKind(uint8_t linkKind) const {
    for (const auto& e : entries)
        if (e.linkKind == linkKind) return &e;
    return nullptr;
}

bool WoweeChatLinksLoader::save(const WoweeChatLinks& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeChatLinks::Entry& e) {
        writePOD(os, e.linkId);
        writeStr(os, e.name);
        writePOD(os, e.linkKind);
        writePOD(os, e.requireServerLookup);
        writePOD(os, e.pad0);
        writePOD(os, e.colorRGBA);
        writeStr(os, e.linkTemplate);
        writeStr(os, e.tooltipTemplate);
        writeStr(os, e.iconRule);
                       });
}

WoweeChatLinks WoweeChatLinksLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeChatLinks>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeChatLinks::Entry& e) {
        if (!readPOD(is, e.linkId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.linkKind) ||
            !readPOD(is, e.requireServerLookup) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.colorRGBA)) { return false; }
        if (!readStr(is, e.linkTemplate) ||
            !readStr(is, e.tooltipTemplate) ||
            !readStr(is, e.iconRule)) { return false; }
                                  return true;
                              });
}

bool WoweeChatLinksLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeChatLinks::Entry makeLink(
    uint32_t linkId, const char* name,
    uint8_t linkKind, uint8_t serverLookup,
    uint32_t colorRGBA,
    const char* linkTemplate,
    const char* tooltipTemplate,
    const char* iconRule) {
    WoweeChatLinks::Entry e;
    e.linkId = linkId; e.name = name;
    e.linkKind = linkKind;
    e.requireServerLookup = serverLookup;
    e.colorRGBA = colorRGBA;
    e.linkTemplate = linkTemplate;
    e.tooltipTemplate = tooltipTemplate;
    e.iconRule = iconRule;
    return e;
}

} // namespace

WoweeChatLinks WoweeChatLinksLoader::makeStandardLinks(
    const std::string& catalogName) {
    using L = WoweeChatLinks;
    WoweeChatLinks c;
    c.name = catalogName;
    // Item link: classic 4-rune-slot template
    // |cffFFFFFF|Hitem:itemId:enchant:gem1:gem2|h
    // [Name]|h|r. Quality color is white default;
    // quality variants override (see makeColor
    // Variants).
    c.entries.push_back(makeLink(
        1, "Item Hyperlink (Common)",
        L::Item, 1 /* server lookup for item data */,
        0xFFFFFFFFu /* white */,
        "|cffffffff|Hitem:%d:%d:%d:%d|h[%s]|h|r",
        "%s",
        "inv"));
    // Quest link: |cffffff00|Hquest:questId:level|h - the comment here said
    // 808080, which is not the colour the line below writes.
    // [Name]|h|r. Gray color for completable quests.
    c.entries.push_back(makeLink(
        2, "Quest Hyperlink",
        L::Quest, 0,
        0xFFFFFF00u /* yellow - quest color */,
        "|cffffff00|Hquest:%d:%d|h[%s]|h|r",
        "Level %d quest",
        "questmark"));
    // Spell link.
    c.entries.push_back(makeLink(
        3, "Spell Hyperlink",
        L::Spell, 0,
        // ff71d5ff, which is what retail writes a spell link in. This said
        // white, and the spellbook said gold: three colours for one link.
        0xFF71D5FFu,
        "|cff71d5ff|Hspell:%d|h[%s]|h|r",
        "%s",
        "spell"));
    // Achievement link.
    c.entries.push_back(makeLink(
        4, "Achievement Hyperlink",
        L::Achievement, 1 /* server lookup for
                            completion state */,
        0xFFFFFF00u /* yellow */,
        "|cffffff00|Hachievement:%d:%s:%d:%d:%d:%d:%d:%d:%d|h[%s]|h|r",
        "%s (%d points)",
        "achievement"));
    return c;
}

WoweeChatLinks WoweeChatLinksLoader::makeTalentTrade(
    const std::string& catalogName) {
    using L = WoweeChatLinks;
    WoweeChatLinks c;
    c.name = catalogName;
    // Talent link: green color (passive enhancements).
    c.entries.push_back(makeLink(
        10, "Talent Hyperlink",
        L::Talent, 0,
        0xFF00FF00u /* green */,
        "|cff00ff00|Htalent:%d:%d|h[%s]|h|r",
        "%s (rank %d)",
        "talent"));
    // Trade-skill recipe: orange color (rare-quality
    // recipe).
    c.entries.push_back(makeLink(
        11, "Trade Recipe Hyperlink",
        L::Trade, 1 /* server lookup for ingredients
                       list */,
        0xFFFFA500u /* orange */,
        "|cffffa500|Htrade:%d:%d:%d:%s|h[%s]|h|r",
        "%s - requires %d %s skill",
        "trade"));
    return c;
}

WoweeChatLinks WoweeChatLinksLoader::makeColorVariants(
    const std::string& catalogName) {
    using L = WoweeChatLinks;
    WoweeChatLinks c;
    c.name = catalogName;
    // Three Item-kind variants distinguished by
    // quality color. The chat composer picks which
    // variant by item quality at link time.
    c.entries.push_back(makeLink(
        20, "Item Common (gray)",
        L::Item, 1, 0xFF9D9D9D /* gray quality */,
        "|cff9d9d9d|Hitem:%d:%d:%d:%d|h[%s]|h|r",
        "%s",
        "inv"));
    c.entries.push_back(makeLink(
        21, "Item Epic (purple)",
        L::Item, 1, 0xFFA335EE /* purple */,
        "|cffa335ee|Hitem:%d:%d:%d:%d|h[%s]|h|r",
        "%s (Epic)",
        "inv"));
    c.entries.push_back(makeLink(
        22, "Item Legendary (orange)",
        L::Item, 1, 0xFFFF8000 /* orange */,
        "|cffff8000|Hitem:%d:%d:%d:%d|h[%s]|h|r",
        "%s (Legendary)",
        "inv"));
    return c;
}

} // namespace pipeline
} // namespace wowee
