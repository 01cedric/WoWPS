#include "pipeline/wowee_auction_houses.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'U', 'H'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wauh";

} // namespace

const WoweeAuctionHouses::Entry*
WoweeAuctionHouses::findById(uint32_t ahId) const {
    for (const auto& e : entries)
        if (e.ahId == ahId) return &e;
    return nullptr;
}

const WoweeAuctionHouses::Entry*
WoweeAuctionHouses::findByNpc(uint32_t npcId) const {
    for (const auto& e : entries)
        if (e.npcAuctioneerId == npcId) return &e;
    return nullptr;
}

std::vector<const WoweeAuctionHouses::Entry*>
WoweeAuctionHouses::findByFaction(uint8_t faction) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.factionAccess == Both ||
            e.factionAccess == Neutral ||
            (faction != Both && e.factionAccess == faction)) {
            out.push_back(&e);
        }
    }
    return out;
}

bool WoweeAuctionHousesLoader::save(
    const WoweeAuctionHouses& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.ahId);
        writeStr(os, e.name);
        writePOD(os, e.factionAccess);
        writePOD(os, e.pad0);
        writePOD(os, e.depositRatePct);
        writePOD(os, e.cutPct);
        writePOD(os, e.minListingDurationHours);
        writePOD(os, e.maxListingDurationHours);
        writePOD(os, e.pad1);
        writePOD(os, e.feePerSlot);
        writePOD(os, e.npcAuctioneerId);
    });
}

WoweeAuctionHouses WoweeAuctionHousesLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeAuctionHouses>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeAuctionHouses::Entry& e) {
        if (!readPOD(is, e.ahId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.factionAccess) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.depositRatePct) ||
            !readPOD(is, e.cutPct) ||
            !readPOD(is, e.minListingDurationHours) ||
            !readPOD(is, e.maxListingDurationHours) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.feePerSlot) ||
            !readPOD(is, e.npcAuctioneerId)) { return false; }
                                  return true;
                              });
}

bool WoweeAuctionHousesLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeAuctionHouses::Entry makeAH(
    uint32_t ahId, const char* name,
    uint8_t factionAccess,
    uint16_t depositRatePct,
    uint16_t cutPct,
    uint16_t minHours, uint16_t maxHours,
    uint32_t feePerSlot,
    uint32_t npcAuctioneerId) {
    WoweeAuctionHouses::Entry e;
    e.ahId = ahId; e.name = name;
    e.factionAccess = factionAccess;
    e.depositRatePct = depositRatePct;
    e.cutPct = cutPct;
    e.minListingDurationHours = minHours;
    e.maxListingDurationHours = maxHours;
    e.feePerSlot = feePerSlot;
    e.npcAuctioneerId = npcAuctioneerId;
    return e;
}

} // namespace

WoweeAuctionHouses WoweeAuctionHousesLoader::makeStormwindAH(
    const std::string& catalogName) {
    using A = WoweeAuctionHouses;
    WoweeAuctionHouses c;
    c.name = catalogName;
    // Stormwind Trade District AH. Vanilla rates:
    // 5% deposit, 5% cut, 12/24/48 hr tiers, no
    // per-slot fee. NPC: Auctioneer Tricket
    // (creatureId 8666).
    c.entries.push_back(makeAH(
        1, "Stormwind Trade District AH",
        A::Alliance,
        500 /* 5% deposit */,
        500 /* 5% cut */,
        12, 48,
        0,
        8666 /* Auctioneer Tricket */));
    return c;
}

WoweeAuctionHouses WoweeAuctionHousesLoader::makeOrgrimmarAH(
    const std::string& catalogName) {
    using A = WoweeAuctionHouses;
    WoweeAuctionHouses c;
    c.name = catalogName;
    // Orgrimmar Valley of Strength AH. Same rates as
    // Stormwind. NPC: Auctioneer Tahesh
    // (creatureId 9856).
    c.entries.push_back(makeAH(
        2, "Orgrimmar Valley of Strength AH",
        A::Horde,
        500 /* 5% deposit */,
        500 /* 5% cut */,
        12, 48,
        0,
        9856 /* Auctioneer Tahesh */));
    return c;
}

WoweeAuctionHouses WoweeAuctionHousesLoader::makeBootyBayAH(
    const std::string& catalogName) {
    using A = WoweeAuctionHouses;
    WoweeAuctionHouses c;
    c.name = catalogName;
    // Booty Bay neutral AH. The famous 15% deposit
    // + 15% cut rates that made cross-faction
    // trading expensive. NPC: Auctioneer Beardo
    // (creatureId 9858).
    c.entries.push_back(makeAH(
        3, "Booty Bay Neutral AH",
        A::Neutral,
        1500 /* 15% deposit */,
        1500 /* 15% cut */,
        12, 48,
        0,
        9858 /* Auctioneer Beardo */));
    return c;
}

} // namespace pipeline
} // namespace wowee
