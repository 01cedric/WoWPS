#include "pipeline/wowee_tutorial_steps.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'U', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wtur";

} // namespace

const WoweeTutorialSteps::Entry*
WoweeTutorialSteps::findById(uint32_t tutId) const {
    for (const auto& e : entries)
        if (e.tutId == tutId) return &e;
    return nullptr;
}

std::vector<const WoweeTutorialSteps::Entry*>
WoweeTutorialSteps::findByEvent(uint8_t triggerEvent) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.triggerEvent == triggerEvent) out.push_back(&e);
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->stepIndex < b->stepIndex;
              });
    return out;
}

bool WoweeTutorialStepsLoader::save(const WoweeTutorialSteps& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeTutorialSteps::Entry& e) {
        writePOD(os, e.tutId);
        writeStr(os, e.name);
        writePOD(os, e.stepIndex);
        writePOD(os, e.triggerEvent);
        writePOD(os, e.pad0);
        writePOD(os, e.triggerValue);
        writePOD(os, e.iconIndex);
        writePOD(os, e.hideAfterSec);
        writeStr(os, e.title);
        writeStr(os, e.body);
        writeStr(os, e.targetUIElementName);
                       });
}

WoweeTutorialSteps WoweeTutorialStepsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeTutorialSteps>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeTutorialSteps::Entry& e) {
        if (!readPOD(is, e.tutId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.stepIndex) ||
            !readPOD(is, e.triggerEvent) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.triggerValue) ||
            !readPOD(is, e.iconIndex) ||
            !readPOD(is, e.hideAfterSec)) { return false; }
        if (!readStr(is, e.title) ||
            !readStr(is, e.body) ||
            !readStr(is, e.targetUIElementName)) { return false; }
                                  return true;
                              });
}

bool WoweeTutorialStepsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeTutorialSteps::Entry makeStep(
    uint32_t tutId, const char* name,
    uint8_t stepIndex, uint8_t triggerEvent,
    uint32_t triggerValue, uint32_t iconIndex,
    uint32_t hideAfterSec,
    const char* title, const char* body,
    const char* uiElement) {
    WoweeTutorialSteps::Entry e;
    e.tutId = tutId; e.name = name;
    e.stepIndex = stepIndex;
    e.triggerEvent = triggerEvent;
    e.triggerValue = triggerValue;
    e.iconIndex = iconIndex;
    e.hideAfterSec = hideAfterSec;
    e.title = title;
    e.body = body;
    e.targetUIElementName = uiElement;
    return e;
}

} // namespace

WoweeTutorialSteps WoweeTutorialStepsLoader::makeNewbieFlow(
    const std::string& catalogName) {
    using T = WoweeTutorialSteps;
    WoweeTutorialSteps c;
    c.name = catalogName;
    // Login trigger (event=0, value=0). 5 sequential
    // steps for the first-time login experience.
    // hideAfterSec=30 gives ~30s to read each tip.
    c.entries.push_back(makeStep(
        1, "Welcome", 1, T::Login, 0, 100, 30,
        "Welcome to Wowee",
        "Welcome to your first adventure. Press any "
        "movement key (W/A/S/D) to start exploring.",
        "MovementHint"));
    c.entries.push_back(makeStep(
        2, "Camera", 2, T::Login, 0, 100, 30,
        "Camera Controls",
        "Hold the right mouse button and drag to look "
        "around. Scroll to zoom in and out.",
        "CameraHint"));
    c.entries.push_back(makeStep(
        3, "Interact", 3, T::Login, 0, 100, 30,
        "Talking to NPCs",
        "Right-click on yellow-name NPCs to talk. "
        "Yellow exclamation marks (!) mean a quest is "
        "available.",
        "QuestNpcMarker"));
    c.entries.push_back(makeStep(
        4, "QuestLog", 4, T::Login, 0, 100, 30,
        "Open Your Quest Log",
        "Press L to open the quest log. Track quests "
        "to see objectives on your minimap.",
        "QuestLogButton"));
    c.entries.push_back(makeStep(
        5, "Inventory", 5, T::Login, 0, 100, 30,
        "Open Your Bags",
        "Press B (or click the bag icon at bottom-"
        "right) to open your inventory. Right-click "
        "items to use or equip them.",
        "BagButton"));
    return c;
}

WoweeTutorialSteps WoweeTutorialStepsLoader::makeLevelUpFlow(
    const std::string& catalogName) {
    using T = WoweeTutorialSteps;
    WoweeTutorialSteps c;
    c.name = catalogName;
    // LevelUp trigger (event=2). triggerValue is the
    // level reached. 3 milestone hints.
    c.entries.push_back(makeStep(
        10, "FirstLevelUp", 1, T::LevelUp, 2, 200, 60,
        "First Level Up",
        "You leveled up. Open your spellbook (P) to "
        "see new abilities your class learns at this "
        "level. Some spells require visiting your "
        "class trainer in a capital city.",
        "SpellbookButton"));
    c.entries.push_back(makeStep(
        11, "TrainerVisit", 2, T::LevelUp, 5, 200, 60,
        "Visit Your Trainer",
        "At level 5 your class trainer in the nearest "
        "capital city has new spells to teach. Class "
        "trainers stand in faction-themed buildings "
        "(Stormwind: Cathedral District for Priests, "
        "Mage District for Mages, etc.).",
        "TrainerNpcMarker"));
    c.entries.push_back(makeStep(
        12, "TalentUnlock", 3, T::LevelUp, 10, 200, 60,
        "Talent Tree Unlocked",
        "You can now spend your first talent point. "
        "Press N to open the talent tree and pick a "
        "specialization. Choose carefully - talents "
        "cost gold to refund.",
        "TalentButton"));
    return c;
}

WoweeTutorialSteps WoweeTutorialStepsLoader::makeBgFlow(
    const std::string& catalogName) {
    using T = WoweeTutorialSteps;
    WoweeTutorialSteps c;
    c.name = catalogName;
    // ZoneEnter trigger (event=1). triggerValue is
    // the WMS mapId for the BG. 3 BG-onboarding
    // tips. mapId 30=AV, 489=WSG, 529=AB.
    c.entries.push_back(makeStep(
        20, "FirstAV", 1, T::ZoneEnter, 30, 300, 45,
        "Welcome to Alterac Valley",
        "Alterac Valley is a 40-vs-40 large-scale "
        "battleground. Your goal is to push to the "
        "enemy general (Drek'Thar for Alliance, Vanndar "
        "for Horde) and defeat them. Capture graveyards "
        "and bunkers along the way.",
        "AVObjectiveTracker"));
    c.entries.push_back(makeStep(
        21, "FirstWSG", 1, T::ZoneEnter, 489, 300, 45,
        "Welcome to Warsong Gulch",
        "Warsong is a 10-vs-10 capture-the-flag map. "
        "Grab the enemy flag from their base and return "
        "it to your own. First team to 3 captures wins.",
        "WSGFlagDisplay"));
    c.entries.push_back(makeStep(
        22, "FirstAB", 1, T::ZoneEnter, 529, 300, 45,
        "Welcome to Arathi Basin",
        "Arathi Basin is 15-vs-15 control-point. Capture "
        "and hold 3+ of the 5 resource nodes (Stables / "
        "Lumber Mill / Blacksmith / Mine / Farm) to "
        "earn resources over time. First team to 1600 "
        "resources wins.",
        "ABScoreDisplay"));
    return c;
}

} // namespace pipeline
} // namespace wowee
