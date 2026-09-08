#include "pipeline/wowee_cinematics.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'M', 'S'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcms";

} // namespace

const WoweeCinematic::Entry*
WoweeCinematic::findById(uint32_t cinematicId) const {
    for (const auto& e : entries) if (e.cinematicId == cinematicId) return &e;
    return nullptr;
}

const char* WoweeCinematic::kindName(uint8_t k) {
    switch (k) {
        case PreRenderedVideo: return "video";
        case CameraFlythrough: return "camera";
        case TextCrawl:        return "text-crawl";
        case StillImage:       return "image";
        case Slideshow:        return "slideshow";
        default:               return "unknown";
    }
}

const char* WoweeCinematic::triggerKindName(uint8_t t) {
    switch (t) {
        case Manual:            return "manual";
        case QuestStart:        return "quest-start";
        case QuestEnd:          return "quest-end";
        case ClassStart:        return "class-start";
        case ZoneEntry:         return "zone-entry";
        case DungeonClear:      return "dungeon-clear";
        case Login:             return "login";
        case AchievementGained: return "achievement";
        case LevelUp:           return "level-up";
        default:                return "unknown";
    }
}

bool WoweeCinematicLoader::save(const WoweeCinematic& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCinematic::Entry& e) {
        writePOD(os, e.cinematicId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.mediaPath);
        writePOD(os, e.kind);
        writePOD(os, e.triggerKind);
        writePOD(os, e.skippable);
        writePadding(os, 1);
        writePOD(os, e.durationSeconds);
        writePOD(os, e.triggerTargetId);
        writePOD(os, e.soundtrackId);
                       });
}

WoweeCinematic WoweeCinematicLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCinematic>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCinematic::Entry& e) {
        if (!readPOD(is, e.cinematicId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.mediaPath)) { return false; }
        if (!readPOD(is, e.kind) ||
            !readPOD(is, e.triggerKind) ||
            !readPOD(is, e.skippable)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.durationSeconds) ||
            !readPOD(is, e.triggerTargetId) ||
            !readPOD(is, e.soundtrackId)) { return false; }
                                  return true;
                              });
}

bool WoweeCinematicLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeCinematic WoweeCinematicLoader::makeStarter(const std::string& catalogName) {
    WoweeCinematic c;
    c.name = catalogName;
    {
        WoweeCinematic::Entry e;
        e.cinematicId = 1; e.name = "Realm Intro";
        e.description = "Pre-rendered intro played on character login.";
        e.kind = WoweeCinematic::PreRenderedVideo;
        e.triggerKind = WoweeCinematic::Login;
        e.mediaPath = "Movies/Intro/realm_intro.ogv";
        e.durationSeconds = 90;
        e.skippable = 1;
        e.soundtrackId = 2;       // WSND.makeStarter music id
        c.entries.push_back(e);
    }
    {
        WoweeCinematic::Entry e;
        e.cinematicId = 2; e.name = "Quest Cutscene";
        e.description = "In-engine camera flythrough on quest accept.";
        e.kind = WoweeCinematic::CameraFlythrough;
        e.triggerKind = WoweeCinematic::QuestStart;
        e.mediaPath = "Cinematics/quest_001_camera.m2";
        e.durationSeconds = 30;
        e.skippable = 1;
        e.triggerTargetId = 1;    // WQT questId 1
        c.entries.push_back(e);
    }
    {
        WoweeCinematic::Entry e;
        e.cinematicId = 3; e.name = "Login Splash";
        e.description = "Static splash image shown on title screen.";
        e.kind = WoweeCinematic::StillImage;
        e.triggerKind = WoweeCinematic::Manual;
        e.mediaPath = "Splash/login_image.png";
        e.durationSeconds = 5;
        c.entries.push_back(e);
    }
    return c;
}

WoweeCinematic WoweeCinematicLoader::makeIntros(const std::string& catalogName) {
    WoweeCinematic c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t classId,
                    const char* media) {
        WoweeCinematic::Entry e;
        e.cinematicId = id; e.name = name;
        e.description = std::string("First-login intro for the ") +
                         name + " class.";
        e.kind = WoweeCinematic::CameraFlythrough;
        e.triggerKind = WoweeCinematic::ClassStart;
        e.mediaPath = media;
        e.durationSeconds = 45;
        e.skippable = 1;
        // triggerTargetId values match WCHC class IDs
        // (1=Warrior, 3=Hunter, 4=Rogue, 8=Mage).
        e.triggerTargetId = classId;
        c.entries.push_back(e);
    };
    add(100, "Warrior", 1, "Cinematics/intro_warrior.m2");
    add(101, "Hunter",  3, "Cinematics/intro_hunter.m2");
    add(102, "Rogue",   4, "Cinematics/intro_rogue.m2");
    add(103, "Mage",    8, "Cinematics/intro_mage.m2");
    return c;
}

WoweeCinematic WoweeCinematicLoader::makeQuestCinematics(const std::string& catalogName) {
    WoweeCinematic c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint8_t kind, const char* name,
                    uint32_t questId, uint8_t triggerKind,
                    uint32_t durSec) {
        WoweeCinematic::Entry e;
        e.cinematicId = id; e.name = name;
        e.kind = WoweeCinematic::CameraFlythrough;
        e.triggerKind = triggerKind;
        e.mediaPath = std::string("Cinematics/quest_") +
                       std::to_string(questId) + ".m2";
        e.durationSeconds = durSec;
        e.skippable = 1;
        e.triggerTargetId = questId;
        (void)kind;
        c.entries.push_back(e);
    };
    // questIds 1 / 100 / 102 match WQT.makeStarter + makeChain.
    add(200, 0, "Bandit Trouble Intro", 1,
        WoweeCinematic::QuestStart, 25);
    add(201, 0, "Investigate Camp Briefing", 100,
        WoweeCinematic::QuestStart, 30);
    add(202, 0, "Bandit Trouble Resolution", 102,
        WoweeCinematic::QuestEnd, 40);
    return c;
}

} // namespace pipeline
} // namespace wowee
