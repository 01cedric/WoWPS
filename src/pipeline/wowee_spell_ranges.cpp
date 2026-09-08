#include "pipeline/wowee_spell_ranges.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'R', 'G'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wsrg";

} // namespace

const WoweeSpellRange::Entry*
WoweeSpellRange::findById(uint32_t rangeId) const {
    for (const auto& e : entries)
        if (e.rangeId == rangeId) return &e;
    return nullptr;
}

const char* WoweeSpellRange::rangeKindName(uint8_t k) {
    switch (k) {
        case Self:        return "self";
        case Melee:       return "melee";
        case ShortRanged: return "short";
        case Ranged:      return "ranged";
        case LongRanged:  return "long";
        case VeryLong:    return "very-long";
        case Unlimited:   return "unlimited";
        default:          return "unknown";
    }
}

bool WoweeSpellRangeLoader::save(const WoweeSpellRange& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellRange::Entry& e) {
        writePOD(os, e.rangeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.rangeKind);
        writePadding(os, 3);
        writePOD(os, e.minRange);
        writePOD(os, e.maxRange);
        writePOD(os, e.minRangeFriendly);
        writePOD(os, e.maxRangeFriendly);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellRange WoweeSpellRangeLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellRange>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellRange::Entry& e) {
        if (!readPOD(is, e.rangeId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.rangeKind)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.minRange) ||
            !readPOD(is, e.maxRange) ||
            !readPOD(is, e.minRangeFriendly) ||
            !readPOD(is, e.maxRangeFriendly) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellRangeLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellRange WoweeSpellRangeLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellRange c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    float minR, float maxR, uint8_t r, uint8_t g,
                    uint8_t b, const char* desc) {
        WoweeSpellRange::Entry e;
        e.rangeId = id; e.name = name; e.description = desc;
        e.rangeKind = kind;
        e.minRange = minR; e.maxRange = maxR;
        // Default friendly == hostile.
        e.minRangeFriendly = minR; e.maxRangeFriendly = maxR;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "SelfRange",     WoweeSpellRange::Self,
        0.0f, 0.0f,  240, 240, 240,
        "Self-only - caster is the only valid target.");
    add(2, "MeleeRange",    WoweeSpellRange::Melee,
        0.0f, 5.0f,  220, 80, 80,
        "Melee - within white-attack range (5y).");
    add(3, "SpellRange",    WoweeSpellRange::Ranged,
        0.0f, 30.0f, 100, 180, 240,
        "Standard spell - 30 yards, common caster range.");
    return c;
}

WoweeSpellRange WoweeSpellRangeLoader::makeRanged(
    const std::string& catalogName) {
    WoweeSpellRange c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    float maxR, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellRange::Entry e;
        e.rangeId = id; e.name = name; e.description = desc;
        e.rangeKind = kind;
        e.maxRange = maxR;
        e.maxRangeFriendly = maxR;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(100, "ShortCast",  WoweeSpellRange::ShortRanged,  20.0f,
        100, 200, 240, "Short-range spell - 20y. Close-up casts.");
    add(101, "MediumCast", WoweeSpellRange::Ranged,       30.0f,
        100, 180, 240, "Medium-range spell - 30y. Default caster range.");
    add(102, "LongCast",   WoweeSpellRange::LongRanged,   40.0f,
        100, 160, 240, "Long-range spell - 40y. Hunter / sniper range.");
    add(103, "VeryLong",   WoweeSpellRange::VeryLong,     100.0f,
        100, 140, 240, "Very-long range - 100y. Vision / aura range.");
    add(104, "Unlimited",  WoweeSpellRange::Unlimited,    99999.0f,
        140, 100, 240, "Unlimited range - global server-tracked spell.");
    return c;
}

WoweeSpellRange WoweeSpellRangeLoader::makeFriendly(
    const std::string& catalogName) {
    WoweeSpellRange c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float maxFriendly,
                    float maxHostile, const char* desc) {
        WoweeSpellRange::Entry e;
        e.rangeId = id; e.name = name; e.description = desc;
        e.rangeKind = WoweeSpellRange::Ranged;
        // Friendly range is the larger of the two.
        e.maxRange = maxHostile;
        e.maxRangeFriendly = maxFriendly;
        e.iconColorRGBA = packRgba(80, 240, 100);   // green for healing
        c.entries.push_back(e);
    };
    add(200, "HealRange",     40.0f,  0.0f,
        "Heal target - 40y friendly, 0y hostile (heals don't "
        "reach enemies).");
    add(201, "CleanseRange",  30.0f,  0.0f,
        "Cleanse / dispel - 30y friendly only.");
    add(202, "BuffRange",     30.0f,  0.0f,
        "Beneficial buff - 30y friendly only (Power Word: "
        "Fortitude, Mark of the Wild).");
    return c;
}

} // namespace pipeline
} // namespace wowee
