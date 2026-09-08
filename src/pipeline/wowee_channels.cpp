#include "pipeline/wowee_channels.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'H', 'N'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wchn";

} // namespace

const WoweeChannel::Entry*
WoweeChannel::findById(uint32_t channelId) const {
    for (const auto& e : entries) if (e.channelId == channelId) return &e;
    return nullptr;
}

const char* WoweeChannel::channelTypeName(uint8_t t) {
    switch (t) {
        case AreaLocal:       return "local";
        case Zone:            return "zone";
        case Continent:       return "continent";
        case World:           return "world";
        case Trade:           return "trade";
        case LookingForGroup: return "lfg";
        case GuildRecruit:    return "guild-recruit";
        case LocalDefense:    return "local-defense";
        case Custom:          return "custom";
        case Pvp:             return "pvp";
        default:              return "unknown";
    }
}

const char* WoweeChannel::factionAccessName(uint8_t f) {
    switch (f) {
        case Alliance: return "alliance";
        case Horde:    return "horde";
        case Both:     return "both";
        default:       return "unknown";
    }
}

bool WoweeChannelLoader::save(const WoweeChannel& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeChannel::Entry& e) {
        writePOD(os, e.channelId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.channelType);
        writePOD(os, e.factionAccess);
        writePOD(os, e.autoJoin);
        writePOD(os, e.announce);
        writePOD(os, e.moderated);
        writePadding(os, 1);
        writePOD(os, e.minLevel);
        writePOD(os, e.areaIdGate);
        writePOD(os, e.mapIdGate);
                       });
}

WoweeChannel WoweeChannelLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeChannel>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeChannel::Entry& e) {
        if (!readPOD(is, e.channelId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.channelType) ||
            !readPOD(is, e.factionAccess) ||
            !readPOD(is, e.autoJoin) ||
            !readPOD(is, e.announce) ||
            !readPOD(is, e.moderated)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.areaIdGate) ||
            !readPOD(is, e.mapIdGate)) { return false; }
                                  return true;
                              });
}

bool WoweeChannelLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeChannel WoweeChannelLoader::makeStarter(const std::string& catalogName) {
    WoweeChannel c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    uint8_t autoJ, uint16_t minLvl,
                    const char* desc) {
        WoweeChannel::Entry e;
        e.channelId = id; e.name = name; e.description = desc;
        e.channelType = type; e.autoJoin = autoJ;
        e.minLevel = minLvl;
        c.entries.push_back(e);
    };
    add(1, "General",       WoweeChannel::Zone,            1, 1,
        "Zone-wide chatter; auto-joined for current zone.");
    add(2, "Trade",         WoweeChannel::Trade,           0, 1,
        "Cross-zone trade chatter; opt-in.");
    add(3, "LookingForGroup", WoweeChannel::LookingForGroup, 1, 10,
        "Global LFG queue chat.");
    add(4, "GuildRecruitment", WoweeChannel::GuildRecruit,  0, 10,
        "Recruit-a-guild + hire-a-guild bulletin.");
    return c;
}

WoweeChannel WoweeChannelLoader::makeCity(const std::string& catalogName) {
    WoweeChannel c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    uint8_t fac, uint32_t mapId, uint32_t areaId,
                    const char* desc) {
        WoweeChannel::Entry e;
        e.channelId = id; e.name = name; e.description = desc;
        e.channelType = type; e.factionAccess = fac;
        e.autoJoin = 1;
        e.mapIdGate = mapId; e.areaIdGate = areaId;
        c.entries.push_back(e);
    };
    // mapId 0 + areaId 1 ~ Stormwind City (matches WMS preset).
    add(100, "General - Stormwind",  WoweeChannel::Zone,
        WoweeChannel::Alliance, 0, 1,
        "Stormwind general chat.");
    add(101, "Trade - Stormwind",    WoweeChannel::Trade,
        WoweeChannel::Alliance, 0, 1,
        "Stormwind trade district chat.");
    add(102, "LFG - Stormwind",      WoweeChannel::LookingForGroup,
        WoweeChannel::Alliance, 0, 1,
        "Stormwind LFG within-city queue.");
    add(110, "General - Orgrimmar",  WoweeChannel::Zone,
        WoweeChannel::Horde, 1, 1637,
        "Orgrimmar general chat.");
    add(111, "Trade - Orgrimmar",    WoweeChannel::Trade,
        WoweeChannel::Horde, 1, 1637,
        "Orgrimmar trade district chat.");
    return c;
}

WoweeChannel WoweeChannelLoader::makeModerated(const std::string& catalogName) {
    WoweeChannel c;
    c.name = catalogName;
    {
        WoweeChannel::Entry e;
        e.channelId = 200; e.name = "LocalDefense";
        e.description =
            "Alarm channel - broadcasts when zone is attacked. "
            "Level 10+ auto-joined.";
        e.channelType = WoweeChannel::LocalDefense;
        e.autoJoin = 1; e.minLevel = 10;
        c.entries.push_back(e);
    }
    {
        WoweeChannel::Entry e;
        e.channelId = 201; e.name = "WorldDefense";
        e.description =
            "Cross-zone defense alarm. World boss / invasion broadcast.";
        e.channelType = WoweeChannel::World;
        e.autoJoin = 1; e.minLevel = 10;
        e.moderated = 1;
        c.entries.push_back(e);
    }
    {
        WoweeChannel::Entry e;
        e.channelId = 202; e.name = "RaidCoordination";
        e.description =
            "Custom moderated channel for cross-guild raid runs.";
        e.channelType = WoweeChannel::Custom;
        e.autoJoin = 0; e.minLevel = 60;
        e.moderated = 1;
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
