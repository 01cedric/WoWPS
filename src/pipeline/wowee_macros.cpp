#include "pipeline/wowee_macros.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'A', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wmac";

} // namespace

const WoweeMacro::Entry*
WoweeMacro::findById(uint32_t macroId) const {
    for (const auto& e : entries)
        if (e.macroId == macroId) return &e;
    return nullptr;
}

const char* WoweeMacro::macroKindName(uint8_t k) {
    switch (k) {
        case SystemSlash:    return "system-slash";
        case DefaultMacro:   return "default-macro";
        case PlayerTemplate: return "player-template";
        case GuildMacro:     return "guild-macro";
        case SharedMacro:    return "shared-macro";
        default:             return "unknown";
    }
}

bool WoweeMacroLoader::save(const WoweeMacro& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeMacro::Entry& e) {
        writePOD(os, e.macroId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writeStr(os, e.macroBody);
        writeStr(os, e.bindKey);
        writePOD(os, e.macroKind);
        writePadding(os, 3);
        writePOD(os, e.requiredClassMask);
        writePOD(os, e.maxLength);
        writePadding(os, 2);
                       });
}

WoweeMacro WoweeMacroLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeMacro>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeMacro::Entry& e) {
        if (!readPOD(is, e.macroId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath) || !readStr(is, e.macroBody) ||
            !readStr(is, e.bindKey)) { return false; }
        if (!readPOD(is, e.macroKind)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.requiredClassMask) ||
            !readPOD(is, e.maxLength)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
                                  return true;
                              });
}

bool WoweeMacroLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeMacro WoweeMacroLoader::makeStarter(
    const std::string& catalogName) {
    WoweeMacro c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* body,
                    const char* desc) {
        WoweeMacro::Entry e;
        e.macroId = id; e.name = name;
        e.description = desc; e.macroBody = body;
        e.iconPath = "Interface/Icons/INV_Misc_Note_01.blp";
        e.macroKind = WoweeMacro::SystemSlash;
        c.entries.push_back(e);
    };
    add(1, "Sit",   "/sit",
        "Toggle sitting on the ground.");
    add(2, "Dance", "/dance",
        "Perform race-specific dance emote.");
    add(3, "Target",
        "/target [@mouseover]",
        "Target the unit your mouse is currently hovering over.");
    return c;
}

WoweeMacro WoweeMacroLoader::makeCombat(
    const std::string& catalogName) {
    WoweeMacro c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* body,
                    const char* bindKey, const char* desc) {
        WoweeMacro::Entry e;
        e.macroId = id; e.name = name;
        e.description = desc;
        e.macroBody = body;
        e.bindKey = bindKey;
        e.iconPath = std::string("Interface/Icons/Ability_Warrior_") +
                      name + ".blp";
        e.macroKind = WoweeMacro::PlayerTemplate;
        e.requiredClassMask = 1u << 1;   // WCHC Warrior bit
        c.entries.push_back(e);
    };
    // Multi-line macros encoded with \n inside the body.
    add(100, "HeroicStrikeSpam",
        "#showtooltip Heroic Strike\n"
        "/cast Heroic Strike",
        "1",
        "Single-rank Heroic Strike with #showtooltip - bind to 1.");
    add(101, "Charge",
        "#showtooltip Charge\n"
        "/cast [combat] Intercept; Charge",
        "Q",
        "Auto-switches between Charge and Intercept based on "
        "combat state.");
    add(102, "Intercept",
        "/cast [stance:1/3] Berserker Stance\n"
        "/cast Intercept",
        "T",
        "Stance-dance into Berserker if not already, then Intercept.");
    add(103, "VictoryRush",
        "#showtooltip Victory Rush\n"
        "/cast Victory Rush\n"
        "/cast Bloodthirst",
        "F",
        "Victory Rush with Bloodthirst fallback.");
    return c;
}

WoweeMacro WoweeMacroLoader::makeUtility(
    const std::string& catalogName) {
    WoweeMacro c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* body,
                    const char* desc) {
        WoweeMacro::Entry e;
        e.macroId = id; e.name = name;
        e.description = desc;
        e.macroBody = body;
        e.iconPath = std::string("Interface/Icons/INV_Misc_") +
                      name + ".blp";
        e.macroKind = WoweeMacro::DefaultMacro;
        c.entries.push_back(e);
    };
    add(200, "FollowTarget",
        "/follow [@target]",
        "Follow the unit you have currently targeted.");
    add(201, "MassInvite",
        "/inv %target1\n"
        "/inv %target2\n"
        "/inv %target3\n"
        "/inv %target4",
        "Invite up to four players in one click. Replace "
        "%targetN with player names.");
    add(202, "ReleaseCorpse",
        "/script RepopMe()",
        "Release spirit immediately on death.");
    return c;
}

} // namespace pipeline
} // namespace wowee
