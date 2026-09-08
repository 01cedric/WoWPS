#include "pipeline/wowee_item_qualities.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'I', 'Q', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wiqr";

} // namespace

const WoweeItemQuality::Entry*
WoweeItemQuality::findById(uint32_t qualityId) const {
    for (const auto& e : entries)
        if (e.qualityId == qualityId) return &e;
    return nullptr;
}

bool WoweeItemQuality::canDropAtLevel(uint32_t qualityId,
                                       uint8_t characterLevel) const {
    const Entry* e = findById(qualityId);
    if (!e) return false;
    if (characterLevel < e->minLevelToDrop) return false;
    if (e->maxLevelToDrop != 0 && characterLevel > e->maxLevelToDrop)
        return false;
    return true;
}

bool WoweeItemQualityLoader::save(const WoweeItemQuality& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeItemQuality::Entry& e) {
        writePOD(os, e.qualityId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.nameColorRGBA);
        writePOD(os, e.borderColorRGBA);
        writePOD(os, e.vendorPriceMultiplier);
        writePOD(os, e.minLevelToDrop);
        writePOD(os, e.maxLevelToDrop);
        writePOD(os, e.canBeDisenchanted);
        writePOD(os, e.pad0);
        writeStr(os, e.inventoryBorderTexture);
                       });
}

WoweeItemQuality WoweeItemQualityLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeItemQuality>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeItemQuality::Entry& e) {
        if (!readPOD(is, e.qualityId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.nameColorRGBA) ||
            !readPOD(is, e.borderColorRGBA) ||
            !readPOD(is, e.vendorPriceMultiplier) ||
            !readPOD(is, e.minLevelToDrop) ||
            !readPOD(is, e.maxLevelToDrop) ||
            !readPOD(is, e.canBeDisenchanted) ||
            !readPOD(is, e.pad0)) { return false; }
        if (!readStr(is, e.inventoryBorderTexture)) { return false; }
                                  return true;
                              });
}

bool WoweeItemQualityLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeItemQuality WoweeItemQualityLoader::makeStandard(
    const std::string& catalogName) {
    using Q = WoweeItemQuality;
    WoweeItemQuality c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t r, uint8_t g, uint8_t b,
                    float vendorMul, uint8_t minLvl, uint8_t maxLvl,
                    uint8_t disenchant, const char* texture,
                    const char* desc) {
        Q::Entry e;
        e.qualityId = id; e.name = name; e.description = desc;
        e.nameColorRGBA = packRgba(r, g, b);
        e.borderColorRGBA = packRgba(r, g, b);
        e.vendorPriceMultiplier = vendorMul;
        e.minLevelToDrop = minLvl;
        e.maxLevelToDrop = maxLvl;
        e.canBeDisenchanted = disenchant;
        e.inventoryBorderTexture = texture;
        c.entries.push_back(e);
    };
    // The canonical WoW item quality scale, with hex colors
    // matching the live client. Heirlooms (id 7) are gated
    // to character level 80 in WotLK.
    add(0, "Poor",      0x9d, 0x9d, 0x9d, 0.5f,  1,  0, 0,
        "", "Poor (gray) - junk loot; vendor sells at half price.");
    add(1, "Common",    0xff, 0xff, 0xff, 1.0f,  1,  0, 1,
        "", "Common (white) - basic gear; standard vendor pricing.");
    add(2, "Uncommon",  0x1e, 0xff, 0x00, 1.5f,  1,  0, 1,
        "Border-Uncommon",
        "Uncommon (green) - early-tier quest reward; 50% markup.");
    add(3, "Rare",      0x00, 0x70, 0xdd, 2.0f,  1,  0, 1,
        "Border-Rare",
        "Rare (blue) - dungeon-tier; 2x markup, can be disenchanted.");
    add(4, "Epic",      0xa3, 0x35, 0xee, 4.0f, 60,  0, 1,
        "Border-Epic",
        "Epic (purple) - raid-tier; 4x markup, disenchants to high-tier dust.");
    add(5, "Legendary", 0xff, 0x80, 0x00, 8.0f, 60,  0, 0,
        "Border-Legendary",
        "Legendary (orange) - extremely rare; cannot be disenchanted.");
    add(6, "Artifact",  0xe6, 0xcc, 0x80, 16.0f, 80, 0, 0,
        "Border-Artifact",
        "Artifact (red-gold) - unique, account-bound.");
    add(7, "Heirloom",  0x00, 0xcc, 0xff, 1.0f, 80, 0, 0,
        "Border-Heirloom",
        "Heirloom (cyan) - scales to character level, lvl 80+ unlock.");
    return c;
}

WoweeItemQuality WoweeItemQualityLoader::makeServerCustom(
    const std::string& catalogName) {
    using Q = WoweeItemQuality;
    WoweeItemQuality c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t r, uint8_t g, uint8_t b,
                    float vendorMul, uint8_t disenchant,
                    const char* desc) {
        Q::Entry e;
        e.qualityId = id; e.name = name; e.description = desc;
        e.nameColorRGBA = packRgba(r, g, b);
        e.borderColorRGBA = packRgba(r, g, b);
        e.vendorPriceMultiplier = vendorMul;
        e.canBeDisenchanted = disenchant;
        c.entries.push_back(e);
    };
    // 4 server-custom tiers above the standard 0..7 range.
    add(100, "Junk",       0x33, 0x33, 0x33, 0.1f, 0,
        "Server-custom: cosmetic junk, near-zero vendor price.");
    add(101, "Weekly",     0x80, 0xff, 0x80, 5.0f, 0,
        "Server-custom: drops only from weekly raids, "
        "premium pricing.");
    add(102, "QuestLocked",0xff, 0xff, 0x40, 0.0f, 0,
        "Server-custom: quest-bound, cannot be sold.");
    add(103, "Donator",    0xff, 0x40, 0xff, 0.0f, 0,
        "Server-custom: donor reward, soulbound, unsellable.");
    return c;
}

WoweeItemQuality WoweeItemQualityLoader::makeRaidTiers(
    const std::string& catalogName) {
    using Q = WoweeItemQuality;
    WoweeItemQuality c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t r, uint8_t g, uint8_t b,
                    float vendorMul, uint8_t minLvl,
                    const char* desc) {
        Q::Entry e;
        e.qualityId = id; e.name = name; e.description = desc;
        e.nameColorRGBA = packRgba(r, g, b);
        e.borderColorRGBA = packRgba(r, g, b);
        e.vendorPriceMultiplier = vendorMul;
        e.minLevelToDrop = minLvl;
        e.canBeDisenchanted = 1;
        c.entries.push_back(e);
    };
    // Vanilla raid progression tiers as alternative quality
    // markers - each tier gates at a higher minLevelToDrop
    // and commands a higher vendor multiplier.
    add(200, "Tier1Raid",  0xa3, 0x35, 0xee,  4.0f, 60,
        "Tier 1 raid set (MC / Onyxia) - Epic-color, lvl 60.");
    add(201, "Tier2Raid",  0xc8, 0x4c, 0xff,  6.0f, 60,
        "Tier 2 raid set (BWL) - slightly brighter purple, "
        "premium pricing.");
    add(202, "Tier3Raid",  0xff, 0x80, 0xc8, 10.0f, 60,
        "Tier 3 raid set (Naxx pre-WotLK) - pink-orange, "
        "rarest pre-TBC tier.");
    add(203, "Legendary",  0xff, 0x80, 0x00, 50.0f, 60,
        "Tier-equivalent legendary (Thunderfury / Sulfuras / "
        "Atiesh) - premium economy pricing.");
    return c;
}

} // namespace pipeline
} // namespace wowee
