#include "pipeline/wowee_auction.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'U', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wauc";

} // namespace

const WoweeAuction::Entry* WoweeAuction::findById(uint32_t houseId) const {
    for (const auto& e : entries) if (e.houseId == houseId) return &e;
    return nullptr;
}

const char* WoweeAuction::factionAccessName(uint8_t f) {
    switch (f) {
        case Alliance: return "alliance";
        case Horde:    return "horde";
        case Neutral:  return "neutral";
        case Both:     return "both";
        default:       return "unknown";
    }
}

bool WoweeAuctionLoader::save(const WoweeAuction& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeAuction::Entry& e) {
        writePOD(os, e.houseId);
        writePOD(os, e.auctioneerNpcId);
        writeStr(os, e.name);
        writePOD(os, e.factionAccess);
        writePadding(os, 3);
        writePOD(os, e.baseDepositRateBp);
        writePOD(os, e.houseCutRateBp);
        writePOD(os, e.maxBidCopper);
        writePOD(os, e.shortHours);
        writePOD(os, e.mediumHours);
        writePOD(os, e.longHours);
        writePadding(os, 2);
        writePOD(os, e.shortMultBp);
        writePOD(os, e.mediumMultBp);
        writePOD(os, e.longMultBp);
        writePOD(os, e.disallowedClassMask);
                       });
}

WoweeAuction WoweeAuctionLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeAuction>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeAuction::Entry& e) {
        if (!readPOD(is, e.houseId) ||
            !readPOD(is, e.auctioneerNpcId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.factionAccess)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.baseDepositRateBp) ||
            !readPOD(is, e.houseCutRateBp) ||
            !readPOD(is, e.maxBidCopper) ||
            !readPOD(is, e.shortHours) ||
            !readPOD(is, e.mediumHours) ||
            !readPOD(is, e.longHours)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
        if (!readPOD(is, e.shortMultBp) ||
            !readPOD(is, e.mediumMultBp) ||
            !readPOD(is, e.longMultBp) ||
            !readPOD(is, e.disallowedClassMask)) { return false; }
                                  return true;
                              });
}

bool WoweeAuctionLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeAuction WoweeAuctionLoader::makeStarter(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    {
        WoweeAuction::Entry e;
        e.houseId = 1; e.name = "Starter House";
        e.factionAccess = WoweeAuction::Neutral;
        c.entries.push_back(e);
    }
    return c;
}

WoweeAuction WoweeAuctionLoader::makeFactionPair(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t fac,
                    uint32_t cutBp, uint32_t auctioneerNpc) {
        WoweeAuction::Entry e;
        e.houseId = id; e.name = name;
        e.factionAccess = fac;
        e.houseCutRateBp = cutBp;
        e.auctioneerNpcId = auctioneerNpc;
        c.entries.push_back(e);
    };
    // Faction houses charge 5%; neutral charges the canonical
    // 15% premium for cross-faction trade.
    add(1, "Stormwind",   WoweeAuction::Alliance, 500,  8719);
    add(2, "Orgrimmar",   WoweeAuction::Horde,    500,  8718);
    add(3, "Booty Bay",   WoweeAuction::Neutral,  1500, 2622);
    return c;
}

WoweeAuction WoweeAuctionLoader::makeRestricted(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    {
        WoweeAuction::Entry e;
        e.houseId = 100; e.name = "Restricted House";
        e.factionAccess = WoweeAuction::Both;
        // Disallow Quest items (class 12) and Containers (class 1)
        // and Keys (class 13) - bitmask combination.
        e.disallowedClassMask = (1u << 1) | (1u << 12) | (1u << 13);
        // Tighter durations + lower max bid for testing.
        e.shortHours = 2;
        e.mediumHours = 6;
        e.longHours = 12;
        e.maxBidCopper = 10000000;     // 1000g cap
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
