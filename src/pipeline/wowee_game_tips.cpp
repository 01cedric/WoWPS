#include "pipeline/wowee_game_tips.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'T', 'P'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wgtp";

} // namespace

const WoweeGameTip::Entry*
WoweeGameTip::findById(uint32_t tipId) const {
    for (const auto& e : entries) if (e.tipId == tipId) return &e;
    return nullptr;
}

const char* WoweeGameTip::displayKindName(uint8_t k) {
    switch (k) {
        case LoadingScreen: return "loading-screen";
        case Tutorial:      return "tutorial";
        case TooltipHelp:   return "tooltip-help";
        case Hint:          return "hint";
        default:            return "unknown";
    }
}

bool WoweeGameTipLoader::save(const WoweeGameTip& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeGameTip::Entry& e) {
        writePOD(os, e.tipId);
        writeStr(os, e.name);
        writeStr(os, e.text);
        writeStr(os, e.iconPath);
        writePOD(os, e.displayKind);
        writePadding(os, 3);
        writePOD(os, e.audienceFilter);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.displayWeight);
        writePadding(os, 2);
        writePOD(os, e.conditionId);
        writePOD(os, e.requiredClassMask);
                       });
}

WoweeGameTip WoweeGameTipLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeGameTip>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeGameTip::Entry& e) {
        if (!readPOD(is, e.tipId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.text) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.displayKind)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.audienceFilter) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.displayWeight)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
        if (!readPOD(is, e.conditionId) ||
            !readPOD(is, e.requiredClassMask)) { return false; }
                                  return true;
                              });
}

bool WoweeGameTipLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeGameTip WoweeGameTipLoader::makeStarter(
    const std::string& catalogName) {
    WoweeGameTip c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* text,
                    uint16_t weight) {
        WoweeGameTip::Entry e;
        e.tipId = id; e.name = name; e.text = text;
        e.iconPath = "Interface/TipOfTheDay/icon_generic.blp";
        e.displayKind = WoweeGameTip::LoadingScreen;
        e.displayWeight = weight;
        c.entries.push_back(e);
    };
    add(1, "CombatHint",
        "Press <Tab> to cycle through nearby enemies. "
        "Right-click to attack.", 1);
    add(2, "MovementHint",
        "Hold both mouse buttons to move forward without "
        "pressing W. Hold right-click to steer with the mouse.", 1);
    add(3, "QuestHint",
        "Yellow exclamation marks (!) above NPCs mean a "
        "quest is available. Yellow question marks (?) mean "
        "a quest is ready to turn in.", 2);
    return c;
}

WoweeGameTip WoweeGameTipLoader::makeNewPlayer(
    const std::string& catalogName) {
    WoweeGameTip c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* text,
                    uint16_t maxLevel) {
        WoweeGameTip::Entry e;
        e.tipId = id; e.name = name; e.text = text;
        e.iconPath = "Interface/TipOfTheDay/icon_newplayer.blp";
        e.displayKind = WoweeGameTip::Tutorial;
        e.audienceFilter = WoweeGameTip::kAudienceNewPlayer |
                            WoweeGameTip::kAudienceAlliance |
                            WoweeGameTip::kAudienceHorde;
        e.minLevel = 1;
        e.maxLevel = maxLevel;
        e.displayWeight = 5;     // weighted higher for new players
        c.entries.push_back(e);
    };
    add(100, "BindHearthstone",
        "Visit an innkeeper to bind your Hearthstone - it's "
        "the easiest way to return home.", 10);
    add(101, "TalentSpec",
        "At level 10 you can spend talent points. Visit your "
        "class trainer to learn how.", 15);
    add(102, "FirstMount",
        "At level 20 you can ride a mount! Save 1 gold and "
        "visit a mount vendor in your faction's capital.", 25);
    add(103, "QuestLog",
        "Press 'L' to open your quest log. You can track up "
        "to 25 active quests at once.", 15);
    add(104, "ProfessionPick",
        "Visit a profession trainer to learn a primary trade. "
        "You can have two primary professions.", 15);
    return c;
}

WoweeGameTip WoweeGameTipLoader::makeAdvanced(
    const std::string& catalogName) {
    WoweeGameTip c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* text,
                    uint8_t kind, uint32_t audience, uint32_t cond,
                    uint16_t weight) {
        WoweeGameTip::Entry e;
        e.tipId = id; e.name = name; e.text = text;
        e.iconPath = "Interface/TipOfTheDay/icon_advanced.blp";
        e.displayKind = kind;
        e.audienceFilter = audience;
        e.minLevel = 70;
        e.maxLevel = 80;
        e.conditionId = cond;
        e.displayWeight = weight;
        c.entries.push_back(e);
    };
    add(200, "RaidMechanic",
        "Raid bosses telegraph their abilities - watch for "
        "ground markers and mechanic announcements.",
        WoweeGameTip::Hint, WoweeGameTip::kAudiencePvE, 0, 3);
    add(201, "PvPArena",
        "Arena teams require a charter signed by 4 players. "
        "Visit an Arena Battlemaster to start one.",
        WoweeGameTip::TooltipHelp, WoweeGameTip::kAudiencePvP, 0, 2);
    add(202, "DailyProfession",
        "Some professions have daily quests at exalted with "
        "your faction. Check Shattrath and Dalaran daily.",
        WoweeGameTip::LoadingScreen,
        WoweeGameTip::kAudienceAll, 0, 2);
    add(203, "DungeonFinder",
        "Press 'I' to open the Dungeon Finder. It will form a "
        "group across servers and teleport you to the dungeon.",
        WoweeGameTip::Tutorial,
        WoweeGameTip::kAudienceAll, 0, 4);
    return c;
}

} // namespace pipeline
} // namespace wowee
