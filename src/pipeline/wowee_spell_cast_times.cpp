#include "pipeline/wowee_spell_cast_times.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'C', 'T'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wsct";

} // namespace

const WoweeSpellCastTime::Entry*
WoweeSpellCastTime::findById(uint32_t castTimeId) const {
    for (const auto& e : entries)
        if (e.castTimeId == castTimeId) return &e;
    return nullptr;
}

int32_t WoweeSpellCastTime::resolveAtLevel(uint32_t castTimeId,
                                            uint32_t characterLevel) const {
    const Entry* e = findById(castTimeId);
    if (!e) return 0;
    int64_t ms = static_cast<int64_t>(e->baseCastMs) +
                 static_cast<int64_t>(e->perLevelMs) *
                 static_cast<int64_t>(characterLevel);
    if (e->minCastMs != 0 || e->maxCastMs != 0) {
        // Clamp only when bounds are non-trivial - minCastMs=
        // maxCastMs=0 means "no clamp configured" rather than
        // "must be exactly zero".
        if (ms < e->minCastMs) ms = e->minCastMs;
        if (e->maxCastMs > 0 && ms > e->maxCastMs) ms = e->maxCastMs;
    }
    if (ms < 0) ms = 0;
    return static_cast<int32_t>(ms);
}

const char* WoweeSpellCastTime::castKindName(uint8_t k) {
    switch (k) {
        case Instant:     return "instant";
        case Cast:        return "cast";
        case Channel:     return "channel";
        case DelayedCast: return "delayed";
        case ChargeCast:  return "charge";
        default:          return "unknown";
    }
}

bool WoweeSpellCastTimeLoader::save(const WoweeSpellCastTime& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellCastTime::Entry& e) {
        writePOD(os, e.castTimeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.castKind);
        writePadding(os, 3);
        writePOD(os, e.baseCastMs);
        writePOD(os, e.perLevelMs);
        writePOD(os, e.minCastMs);
        writePOD(os, e.maxCastMs);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellCastTime WoweeSpellCastTimeLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellCastTime>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellCastTime::Entry& e) {
        if (!readPOD(is, e.castTimeId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.castKind)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.baseCastMs) ||
            !readPOD(is, e.perLevelMs) ||
            !readPOD(is, e.minCastMs) ||
            !readPOD(is, e.maxCastMs) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellCastTimeLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellCastTime WoweeSpellCastTimeLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellCastTime c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    int32_t baseMs, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellCastTime::Entry e;
        e.castTimeId = id; e.name = name; e.description = desc;
        e.castKind = kind;
        e.baseCastMs = baseMs;
        // Starter buckets do not scale with level and don't
        // clamp - leave perLevel=0, min=0, max=0.
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "Instant",    WoweeSpellCastTime::Instant, 0,
        100, 240, 100, "Instant - fires on cast (0ms).");
    add(2, "FastCast",   WoweeSpellCastTime::Cast, 1000,
        180, 240, 100, "Fast cast - 1.0s base.");
    add(3, "MediumCast", WoweeSpellCastTime::Cast, 1500,
        240, 240, 100, "Medium cast - 1.5s base (Frostbolt rank 1).");
    add(4, "LongCast",   WoweeSpellCastTime::Cast, 3000,
        240, 180, 100, "Long cast - 3.0s base (Pyroblast).");
    return c;
}

WoweeSpellCastTime WoweeSpellCastTimeLoader::makeChannel(
    const std::string& catalogName) {
    WoweeSpellCastTime c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, int32_t baseMs,
                    const char* desc) {
        WoweeSpellCastTime::Entry e;
        e.castTimeId = id; e.name = name; e.description = desc;
        e.castKind = WoweeSpellCastTime::Channel;
        e.baseCastMs = baseMs;
        // Channels are normally not haste-clamped; min/max
        // stay 0 and the engine treats baseCastMs as the
        // total channel duration.
        e.iconColorRGBA = packRgba(180, 100, 240);   // purple
        c.entries.push_back(e);
    };
    add(100, "TickEvery1s",  3000,
        "Channel - 3s total, ticks every 1s (Drain Life).");
    add(101, "TickEvery2s",  6000,
        "Channel - 6s total, ticks every 2s (Mind Flay).");
    add(102, "TickEvery3s",  9000,
        "Channel - 9s total, ticks every 3s (Tranquility).");
    return c;
}

WoweeSpellCastTime WoweeSpellCastTimeLoader::makeRamp(
    const std::string& catalogName) {
    WoweeSpellCastTime c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, int32_t baseMs,
                    int32_t perLevelMs, int32_t minMs, int32_t maxMs,
                    const char* desc) {
        WoweeSpellCastTime::Entry e;
        e.castTimeId = id; e.name = name; e.description = desc;
        e.castKind = WoweeSpellCastTime::Cast;
        e.baseCastMs = baseMs;
        e.perLevelMs = perLevelMs;
        e.minCastMs = minMs;
        e.maxCastMs = maxMs;
        e.iconColorRGBA = packRgba(240, 100, 180);   // pink
        c.entries.push_back(e);
    };
    // baseCastMs is the level-1 value; +perLevelMs per
    // character level, clamped to [minMs, maxMs] for haste
    // and end-game scaling.
    add(200, "ScalingShort",   500,  10,  500,  2000,
        "Level-scaled short cast: 0.5s + 10ms/lvl (clamps "
        "0.5..2.0s).");
    add(201, "ScalingMedium", 1000,  20, 1000,  3000,
        "Level-scaled medium cast: 1.0s + 20ms/lvl (clamps "
        "1.0..3.0s).");
    add(202, "ScalingLong",   2000,  30, 2000,  5000,
        "Level-scaled long cast: 2.0s + 30ms/lvl (clamps "
        "2.0..5.0s).");
    add(203, "ScalingHuge",   3000,  50, 3000, 10000,
        "Level-scaled huge cast: 3.0s + 50ms/lvl (clamps "
        "3.0..10.0s).");
    return c;
}

} // namespace pipeline
} // namespace wowee
