#include "pipeline/wowee_spell_durations.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'D', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wsdr";

} // namespace

const WoweeSpellDuration::Entry*
WoweeSpellDuration::findById(uint32_t durationId) const {
    for (const auto& e : entries)
        if (e.durationId == durationId) return &e;
    return nullptr;
}

int32_t WoweeSpellDuration::resolveAtLevel(uint32_t durationId,
                                            uint32_t casterLevel) const {
    const Entry* e = findById(durationId);
    if (!e) return 0;
    // Sentinel: a negative base (typically -1) means the
    // engine should treat this as "no timer" - UntilCancelled
    // or UntilDeath.
    if (e->baseDurationMs < 0) return -1;
    int64_t ms = static_cast<int64_t>(e->baseDurationMs) +
                 static_cast<int64_t>(e->perLevelMs) *
                 static_cast<int64_t>(casterLevel);
    if (e->maxDurationMs > 0 && ms > e->maxDurationMs)
        ms = e->maxDurationMs;
    if (ms < 0) ms = 0;
    return static_cast<int32_t>(ms);
}

const char* WoweeSpellDuration::durationKindName(uint8_t k) {
    switch (k) {
        case Instant:        return "instant";
        case Timed:          return "timed";
        case TickBased:      return "tick";
        case UntilCancelled: return "until-cancelled";
        case UntilDeath:     return "until-death";
        default:             return "unknown";
    }
}

bool WoweeSpellDurationLoader::save(const WoweeSpellDuration& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellDuration::Entry& e) {
        writePOD(os, e.durationId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.durationKind);
        writePadding(os, 3);
        writePOD(os, e.baseDurationMs);
        writePOD(os, e.perLevelMs);
        writePOD(os, e.maxDurationMs);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellDuration WoweeSpellDurationLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellDuration>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellDuration::Entry& e) {
        if (!readPOD(is, e.durationId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.durationKind)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.baseDurationMs) ||
            !readPOD(is, e.perLevelMs) ||
            !readPOD(is, e.maxDurationMs) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellDurationLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellDuration WoweeSpellDurationLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellDuration c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    int32_t baseMs, int32_t maxMs,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellDuration::Entry e;
        e.durationId = id; e.name = name; e.description = desc;
        e.durationKind = kind;
        e.baseDurationMs = baseMs;
        e.maxDurationMs = maxMs;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "Instant",   WoweeSpellDuration::Instant,        0,        0,
        100, 240, 100, "Instant - fires once, no aura applied.");
    add(2, "Short",     WoweeSpellDuration::Timed,       5000,        0,
        140, 240, 140, "Short - 5s timed effect (snare / brief debuff).");
    add(3, "Medium",    WoweeSpellDuration::Timed,      30000,        0,
        180, 240, 180, "Medium - 30s timed buff/debuff (most procs).");
    add(4, "Long",      WoweeSpellDuration::Timed,     300000,        0,
        220, 240, 100, "Long - 5min timed buff (most class buffs).");
    add(5, "OneHour",   WoweeSpellDuration::Timed,    3600000,  3600000,
        240, 220, 100, "OneHour - 60min capped buff (food / scroll).");
    return c;
}

WoweeSpellDuration WoweeSpellDurationLoader::makeBuffs(
    const std::string& catalogName) {
    WoweeSpellDuration c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    int32_t baseMs, int32_t maxMs,
                    const char* desc) {
        WoweeSpellDuration::Entry e;
        e.durationId = id; e.name = name; e.description = desc;
        e.durationKind = kind;
        e.baseDurationMs = baseMs;
        e.maxDurationMs = maxMs;
        e.iconColorRGBA = packRgba(100, 200, 240);   // light blue
        c.entries.push_back(e);
    };
    add(100, "PartyBuff",   WoweeSpellDuration::Timed,
        1800000,  1800000,  "Party buff - 30 min (Mark of the Wild).");
    add(101, "RaidBuff",    WoweeSpellDuration::Timed,
        3600000,  3600000,  "Raid buff - 60 min (Power Word: "
        "Fortitude).");
    add(102, "WorldBuff",   WoweeSpellDuration::Timed,
        14400000, 14400000, "World buff - 4 hr (Onyxia / "
        "Rallying Cry).");
    add(103, "UntilDeath",  WoweeSpellDuration::UntilDeath,
        -1,             0, "Permanent until target dies "
        "(Soulstone resurrection).");
    return c;
}

WoweeSpellDuration WoweeSpellDurationLoader::makeDot(
    const std::string& catalogName) {
    WoweeSpellDuration c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, int32_t baseMs,
                    int32_t perLevelMs, int32_t maxMs,
                    const char* desc) {
        WoweeSpellDuration::Entry e;
        e.durationId = id; e.name = name; e.description = desc;
        e.durationKind = WoweeSpellDuration::TickBased;
        e.baseDurationMs = baseMs;
        e.perLevelMs = perLevelMs;
        e.maxDurationMs = maxMs;
        e.iconColorRGBA = packRgba(240, 100, 100);   // red for DoT
        c.entries.push_back(e);
    };
    // Tick interval is canonically 3s; baseDuration = ticks * 3000.
    add(200, "DoT4Tick", 12000,  100,  18000,
        "DoT - 4 ticks @ 3s (12s base, +0.1s/lvl, cap 18s).");
    add(201, "DoT5Tick", 15000,  150,  24000,
        "DoT - 5 ticks @ 3s (15s base, +0.15s/lvl, cap 24s).");
    add(202, "DoT6Tick", 18000,  200,  30000,
        "DoT - 6 ticks @ 3s (18s base, +0.2s/lvl, cap 30s).");
    add(203, "DoT8Tick", 24000,  250,  36000,
        "DoT - 8 ticks @ 3s (24s base, +0.25s/lvl, cap 36s).");
    return c;
}

} // namespace pipeline
} // namespace wowee
