#include "pipeline/wowee_npc_services.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'K', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wbkd";

} // namespace

const WoweeNPCService::Entry*
WoweeNPCService::findById(uint32_t serviceId) const {
    for (const auto& e : entries)
        if (e.serviceId == serviceId) return &e;
    return nullptr;
}

std::vector<const WoweeNPCService::Entry*>
WoweeNPCService::findByKind(uint8_t kind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.serviceKind == kind) out.push_back(&e);
    }
    return out;
}

const char* WoweeNPCService::serviceKindName(uint8_t k) {
    switch (k) {
        case Banker:        return "banker";
        case Mailbox:       return "mailbox";
        case Auctioneer:    return "auctioneer";
        case StableMaster:  return "stable-master";
        case FlightMaster:  return "flight-master";
        case Trainer:       return "trainer";
        case Innkeeper:     return "innkeeper";
        case Battlemaster:  return "battlemaster";
        case GuildBanker:   return "guild-banker";
        case ReagentVendor: return "reagent-vendor";
        case TabardVendor:  return "tabard-vendor";
        case Misc:          return "misc";
        default:            return "unknown";
    }
}

bool WoweeNPCServiceLoader::save(const WoweeNPCService& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeNPCService::Entry& e) {
        writePOD(os, e.serviceId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.serviceKind);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.requiresGold);
        writePOD(os, e.factionRequiredId);
        writePOD(os, e.gossipTextId);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeNPCService WoweeNPCServiceLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeNPCService>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeNPCService::Entry& e) {
        if (!readPOD(is, e.serviceId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.serviceKind) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.requiresGold) ||
            !readPOD(is, e.factionRequiredId) ||
            !readPOD(is, e.gossipTextId) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeNPCServiceLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeNPCService WoweeNPCServiceLoader::makeCity(
    const std::string& catalogName) {
    using N = WoweeNPCService;
    WoweeNPCService c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t cop, uint32_t faction, uint32_t gossip,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        N::Entry e;
        e.serviceId = id; e.name = name; e.description = desc;
        e.serviceKind = kind;
        e.requiresGold = cop;
        e.factionRequiredId = faction;
        e.gossipTextId = gossip;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "CityBanker",      N::Banker,       0, 0, 1000,
        220, 220, 100, "City banker - opens 28-slot inventory bank.");
    add(2, "CityMailbox",     N::Mailbox,      0, 0, 0,
        180, 180, 240, "City mailbox - send/receive mail (no NPC).");
    add(3, "CityInnkeeper",   N::Innkeeper,    0, 0, 1500,
        240, 200, 100, "City innkeeper - set hearthstone bind, "
        "rest XP buff.");
    add(4, "CityAuctioneer",  N::Auctioneer,   0, 0, 1200,
        180, 220, 180, "City auctioneer - opens AH (5%% deposit, "
        "5%% sale cut).");
    add(5, "CityFlightMaster",N::FlightMaster, 0, 0, 1100,
        140, 200, 240, "City flight master - taxi node selection.");
    return c;
}

WoweeNPCService WoweeNPCServiceLoader::makeBattle(
    const std::string& catalogName) {
    using N = WoweeNPCService;
    WoweeNPCService c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* desc) {
        N::Entry e;
        e.serviceId = id; e.name = name; e.description = desc;
        e.serviceKind = N::Battlemaster;
        // Battlemasters don't charge gold, but require
        // faction-aligned battleground access.
        e.iconColorRGBA = packRgba(220, 80, 80);   // pvp red
        c.entries.push_back(e);
    };
    add(100, "BattlemasterAV",
        "Alterac Valley battlemaster - 40v40 BG queue.");
    add(101, "BattlemasterWSG",
        "Warsong Gulch battlemaster - 10v10 capture-flag BG queue.");
    add(102, "BattlemasterAB",
        "Arathi Basin battlemaster - 15v15 control-point BG queue.");
    return c;
}

WoweeNPCService WoweeNPCServiceLoader::makeProfession(
    const std::string& catalogName) {
    using N = WoweeNPCService;
    WoweeNPCService c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t cop, const char* desc) {
        N::Entry e;
        e.serviceId = id; e.name = name; e.description = desc;
        e.serviceKind = kind;
        e.requiresGold = cop;
        e.iconColorRGBA = packRgba(180, 140, 80);   // crafting brown
        c.entries.push_back(e);
    };
    add(200, "BlacksmithTrainer", N::Trainer,        0,
        "Blacksmithing trainer - teaches recipes and rank-ups.");
    add(201, "TailoringTrainer",  N::Trainer,        0,
        "Tailoring trainer - teaches cloth crafting recipes.");
    add(202, "ReagentVendor",     N::ReagentVendor,  0,
        "Reagent vendor - sells profession reagents in stacks.");
    add(203, "StableMaster",      N::StableMaster,  100,
        "Stable master - costs 1 silver to swap pets in/out "
        "of stable.");
    return c;
}

} // namespace pipeline
} // namespace wowee
