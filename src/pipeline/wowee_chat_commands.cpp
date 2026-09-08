#include "pipeline/wowee_chat_commands.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'M', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcmd";

} // namespace

const WoweeChatCommands::Entry*
WoweeChatCommands::findById(uint32_t cmdId) const {
    for (const auto& e : entries)
        if (e.cmdId == cmdId) return &e;
    return nullptr;
}

const WoweeChatCommands::Entry*
WoweeChatCommands::findByCommand(const std::string& cmd) const {
    for (const auto& e : entries) {
        if (e.command == cmd) return &e;
        for (const auto& a : e.aliases) {
            if (a == cmd) return &e;
        }
    }
    return nullptr;
}

std::vector<const WoweeChatCommands::Entry*>
WoweeChatCommands::findByMinSecurity(uint8_t playerSec) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.minSecurityLevel <= playerSec)
            out.push_back(&e);
    }
    return out;
}

bool WoweeChatCommandsLoader::save(const WoweeChatCommands& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeChatCommands::Entry& e) {
        writePOD(os, e.cmdId);
        writeStr(os, e.command);
        writePOD(os, e.minSecurityLevel);
        writePOD(os, e.category);
        writePOD(os, e.isHidden);
        writePOD(os, e.pad0);
        writePOD(os, e.throttleMs);
        writeStr(os, e.argSchema);
        writeStr(os, e.helpText);
        uint32_t aliasCount =
            static_cast<uint32_t>(e.aliases.size());
        writePOD(os, aliasCount);
        for (const auto& a : e.aliases) {
            writeStr(os, a);
        }
                       });
}

WoweeChatCommands WoweeChatCommandsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeChatCommands>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeChatCommands::Entry& e) {
        if (!readPOD(is, e.cmdId)) { return false; }
        if (!readStr(is, e.command)) { return false; }
        if (!readPOD(is, e.minSecurityLevel) ||
            !readPOD(is, e.category) ||
            !readPOD(is, e.isHidden) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.throttleMs)) { return false; }
        if (!readStr(is, e.argSchema) ||
            !readStr(is, e.helpText)) { return false; }
        uint32_t aliasCount = 0;
        if (!readPOD(is, aliasCount)) { return false; }
        // Sanity cap - no command should have more
        // than 32 aliases.
        if (aliasCount > 32) { return false; }
        e.aliases.resize(aliasCount);
        for (auto& a : e.aliases) {
            if (!readStr(is, a)) { return false; }
        }
                                  return true;
                              });
}

bool WoweeChatCommandsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeChatCommands::Entry makeCmd(
    uint32_t cmdId, const char* command,
    uint8_t minSec, uint8_t category, uint8_t isHidden,
    uint32_t throttleMs,
    const char* argSchema, const char* helpText,
    std::vector<std::string> aliases) {
    WoweeChatCommands::Entry e;
    e.cmdId = cmdId; e.command = command;
    e.minSecurityLevel = minSec;
    e.category = category;
    e.isHidden = isHidden;
    e.throttleMs = throttleMs;
    e.argSchema = argSchema;
    e.helpText = helpText;
    e.aliases = std::move(aliases);
    return e;
}

} // namespace

WoweeChatCommands WoweeChatCommandsLoader::makeBasicCommands(
    const std::string& catalogName) {
    using W = WoweeChatCommands;
    WoweeChatCommands c;
    c.name = catalogName;
    // Standard player-facing info commands. All
    // Player security level. No throttle (used
    // freely). Aliases provided where vanilla
    // historically supported them.
    c.entries.push_back(makeCmd(
        1, "who", W::Player, W::Info, 0, 0,
        "[name|class|race|zone]",
        "Show players matching the filter "
        "(or all if no filter). Capped at 49 "
        "results in vanilla.",
        {"w"}));
    c.entries.push_back(makeCmd(
        2, "played", W::Player, W::Info, 0, 0,
        "(no args)",
        "Print total /played time for this "
        "character + total time at current level.",
        {}));
    c.entries.push_back(makeCmd(
        3, "time", W::Player, W::Info, 0, 0,
        "(no args)",
        "Print current server time + local "
        "time + uptime.",
        {}));
    c.entries.push_back(makeCmd(
        4, "ginfo", W::Player, W::Info, 0, 0,
        "(no args)",
        "Print guild info: name, MOTD, member "
        "count online/total, current GM.",
        {"guildinfo"}));
    return c;
}

WoweeChatCommands
WoweeChatCommandsLoader::makeMovementCommands(
    const std::string& catalogName) {
    using W = WoweeChatCommands;
    WoweeChatCommands c;
    c.name = catalogName;
    // Emote-style movement commands. Player level,
    // Movement category, no throttle. Each has at
    // least one alias for typing speed.
    c.entries.push_back(makeCmd(
        10, "sit", W::Player, W::Movement, 0, 0,
        "(no args)",
        "Sit down. Regenerates health/mana 33% "
        "faster while sitting.",
        {"sitdown"}));
    c.entries.push_back(makeCmd(
        11, "stand", W::Player, W::Movement, 0, 0,
        "(no args)",
        "Stand up.",
        {"standup", "su"}));
    c.entries.push_back(makeCmd(
        12, "sleep", W::Player, W::Movement, 0, 0,
        "(no args)",
        "Lie down to sleep. Cosmetic only.",
        {"laydown"}));
    return c;
}

WoweeChatCommands WoweeChatCommandsLoader::makeAdminCommands(
    const std::string& catalogName) {
    using W = WoweeChatCommands;
    WoweeChatCommands c;
    c.name = catalogName;
    // GameMaster security with rate-limiting to
    // prevent admin-spam abuse / accidental
    // floods.
    c.entries.push_back(makeCmd(
        20, "announce", W::GameMaster, W::AdminCmd, 0,
        5000 /* 5s throttle */,
        "<message>",
        "Broadcast a server-wide announcement to "
        "all online players. 5s per-GM throttle "
        "to prevent spam.",
        {"broadcast"}));
    c.entries.push_back(makeCmd(
        21, "kick", W::GameMaster, W::AdminCmd, 0,
        2000 /* 2s throttle */,
        "<charname> [reason]",
        "Force a player offline. Reason is sent "
        "as a system message before disconnect. "
        "2s throttle.",
        {}));
    c.entries.push_back(makeCmd(
        22, "ban", W::GameMaster, W::AdminCmd, 0,
        10000 /* 10s throttle - bans are heavy */,
        "<charname> <durationHours> <reason>",
        "Ban an account for the specified duration. "
        "Use durationHours=0 for permanent ban. "
        "Reason is logged. 10s throttle.",
        {}));
    return c;
}

} // namespace pipeline
} // namespace wowee
