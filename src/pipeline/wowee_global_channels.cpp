#include "pipeline/wowee_global_channels.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'C', 'H'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wgch";

} // namespace

const WoweeGlobalChannels::Entry*
WoweeGlobalChannels::findById(uint32_t channelId) const {
    for (const auto& e : entries)
        if (e.channelId == channelId) return &e;
    return nullptr;
}

std::vector<const WoweeGlobalChannels::Entry*>
WoweeGlobalChannels::findByKind(uint8_t channelKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.channelKind == channelKind) out.push_back(&e);
    return out;
}

std::vector<const WoweeGlobalChannels::Entry*>
WoweeGlobalChannels::findAutoJoinForZone(uint32_t mapId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.accessKind == AutoJoinOnZone &&
            e.zoneDefaultMapId == mapId) {
            out.push_back(&e);
        }
    }
    return out;
}

bool WoweeGlobalChannelsLoader::save(
    const WoweeGlobalChannels& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.channelId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.channelKind);
        writePOD(os, e.accessKind);
        writePOD(os, e.passwordRequired);
        writePOD(os, e.levelMin);
        writePOD(os, e.maxMembers);
        writePOD(os, e.topicSetByMods);
        writePOD(os, e.pad0);
        writePOD(os, e.zoneDefaultMapId);
        writePOD(os, e.iconColorRGBA);
    });
}

WoweeGlobalChannels WoweeGlobalChannelsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeGlobalChannels>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeGlobalChannels::Entry& e) {
        if (!readPOD(is, e.channelId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.channelKind) ||
            !readPOD(is, e.accessKind) ||
            !readPOD(is, e.passwordRequired) ||
            !readPOD(is, e.levelMin) ||
            !readPOD(is, e.maxMembers) ||
            !readPOD(is, e.topicSetByMods) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.zoneDefaultMapId) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeGlobalChannelsLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeGlobalChannels
WoweeGlobalChannelsLoader::makeStandardChannels(
    const std::string& catalogName) {
    using G = WoweeGlobalChannels;
    WoweeGlobalChannels c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t access, uint8_t levelMin,
                    uint16_t maxMembers, uint32_t zoneMap,
                    const char* desc) {
        G::Entry e;
        e.channelId = id; e.name = name; e.description = desc;
        e.channelKind = kind;
        e.accessKind = access;
        e.passwordRequired = 0;
        e.levelMin = levelMin;
        e.maxMembers = maxMembers;
        e.topicSetByMods = 1;
        e.zoneDefaultMapId = zoneMap;
        e.iconColorRGBA = packRgba(140, 200, 255);   // chat blue
        c.entries.push_back(e);
    };
    add(1, "LookingForGroup", G::Global, G::PublicJoin,
        15, 0, 0,
        "Server-wide LFG channel - players queue for "
        "5-man dungeons here. Level 15 required (matches "
        "first dungeon access age).");
    add(2, "World", G::Global, G::PublicJoin,
        10, 0, 0,
        "Server-wide World channel - general chat. "
        "Level 10 minimum to filter trial-account spam.");
    add(3, "TradeStormwind", G::RealmZone, G::AutoJoinOnZone,
        0, 0, 1519,
        "Trade chat - auto-enrolled when entering "
        "Stormwind (areaId 1519). RealmZone scoped: "
        "players in other capitals see their own "
        "Trade channel.");
    add(4, "General", G::Global, G::PublicJoin,
        1, 0, 0,
        "Generic catch-all general chat. Level 1 - "
        "open to all characters.");
    return c;
}

WoweeGlobalChannels WoweeGlobalChannelsLoader::makeRoleplay(
    const std::string& catalogName) {
    using G = WoweeGlobalChannels;
    WoweeGlobalChannels c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t access, uint8_t pwReq,
                    uint8_t levelMin, uint16_t maxMembers,
                    const char* desc) {
        G::Entry e;
        e.channelId = id; e.name = name; e.description = desc;
        e.channelKind = kind;
        e.accessKind = access;
        e.passwordRequired = pwReq;
        e.levelMin = levelMin;
        e.maxMembers = maxMembers;
        e.topicSetByMods = 1;
        e.zoneDefaultMapId = 0;
        e.iconColorRGBA = packRgba(180, 100, 240);   // RP purple
        c.entries.push_back(e);
    };
    add(100, "RP_OOC",     G::Custom, G::PublicJoin,
        0, 1, 0,
        "Out-of-character RP chat - meta-discussion "
        "without breaking immersion in the IC channel. "
        "Public join, no level gate.");
    add(101, "RP_IC",      G::Custom, G::Moderated,
        0, 5, 200,
        "In-character RP chat - moderated to enforce "
        "RP-only language. 200-member cap. Mods can "
        "/silence offenders.");
    add(102, "RP_Forum",   G::Custom, G::InviteOnly,
        0, 1, 50,
        "Invite-only RP planning channel. 50-member "
        "cap. Used for guild RP coordination.");
    add(103, "RP_Events",  G::Custom, G::PublicJoin,
        1, 1, 0,
        "RP event announcements - password-protected "
        "to prevent troll spam. Password is shared via "
        "guild forums.");
    return c;
}

WoweeGlobalChannels
WoweeGlobalChannelsLoader::makeAdminChannels(
    const std::string& catalogName) {
    using G = WoweeGlobalChannels;
    WoweeGlobalChannels c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t access, uint16_t maxMembers,
                    const char* desc) {
        G::Entry e;
        e.channelId = id; e.name = name; e.description = desc;
        e.channelKind = G::Custom;
        e.accessKind = access;
        e.passwordRequired = 1;        // all admin
                                        // channels gated
                                        // by password
        e.levelMin = 0;
        e.maxMembers = maxMembers;
        e.topicSetByMods = 1;
        e.zoneDefaultMapId = 0;
        e.iconColorRGBA = packRgba(220, 60, 60);   // admin red
        c.entries.push_back(e);
    };
    add(200, "GMTraffic", G::InviteOnly, 30,
        "GM coordination chat - handles in-game support "
        "tickets. Invite-only with password. 30 member "
        "cap matches typical GM team size.");
    add(201, "AuditLog",  G::Moderated, 50,
        "Read-only audit-log channel - automated GM-"
        "command and trade-window logs broadcast here. "
        "Moderated kind means only the audit bot can "
        "speak; humans only read.");
    add(202, "Backstage", G::InviteOnly, 20,
        "Server admin backstage chat - devops + senior "
        "GM only. Invite-only, password protected, "
        "20-member cap. NOT logged to AuditLog.");
    return c;
}

} // namespace pipeline
} // namespace wowee
