#include "pipeline/wowee_server_config.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'F', 'G'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcfg";

} // namespace

const WoweeServerConfig::Entry*
WoweeServerConfig::findById(uint32_t configId) const {
    for (const auto& e : entries)
        if (e.configId == configId) return &e;
    return nullptr;
}

std::vector<const WoweeServerConfig::Entry*>
WoweeServerConfig::findByKind(uint8_t configKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.configKind == configKind) out.push_back(&e);
    return out;
}

bool WoweeServerConfigLoader::save(const WoweeServerConfig& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeServerConfig::Entry& e) {
        writePOD(os, e.configId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.configKind);
        writePOD(os, e.valueKind);
        writePOD(os, e.restartRequired);
        writePOD(os, e.pad0);
        writePOD(os, e.floatValue);
        writePOD(os, e.intValue);
        writeStr(os, e.strValue);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeServerConfig WoweeServerConfigLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeServerConfig>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeServerConfig::Entry& e) {
        if (!readPOD(is, e.configId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.configKind) ||
            !readPOD(is, e.valueKind) ||
            !readPOD(is, e.restartRequired) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.floatValue) ||
            !readPOD(is, e.intValue)) { return false; }
        if (!readStr(is, e.strValue)) { return false; }
        if (!readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeServerConfigLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeServerConfig WoweeServerConfigLoader::makeRates(
    const std::string& catalogName) {
    using C = WoweeServerConfig;
    WoweeServerConfig c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    float floatVal, const char* desc) {
        C::Entry e;
        e.configId = id; e.name = name; e.description = desc;
        e.configKind = kind;
        e.valueKind = C::Float;
        e.floatValue = floatVal;
        e.iconColorRGBA = packRgba(220, 220, 100);   // rate gold
        c.entries.push_back(e);
    };
    add(1, "XPRateGlobal", C::XPRate, 1.0f,
        "Global experience-rate multiplier - 1.0 = "
        "Blizzard-default rate. Custom-server values "
        "commonly 2.0 (double-XP), 5.0 (low-population "
        "boost), 10.0 (instant-leveling testing).");
    add(2, "DropRateGlobal", C::DropRate, 1.0f,
        "Global loot-drop multiplier - 1.0 = Blizzard "
        "default. Increase to compensate for low-pop "
        "raid-team availability.");
    add(3, "HonorRateGlobal", C::HonorRate, 1.0f,
        "PvP honor-gain multiplier - 1.0 = default. "
        "Custom-server boost to accelerate PvP rank "
        "progression.");
    add(4, "RestedRate200Pct", C::RestedXP, 2.0f,
        "Rested-XP multiplier - 2.0 = double-XP while "
        "in rested state (Blizzard default). Custom "
        "values up to 5.0 for casual-friendly servers.");
    return c;
}

WoweeServerConfig WoweeServerConfigLoader::makePerformance(
    const std::string& catalogName) {
    using C = WoweeServerConfig;
    WoweeServerConfig c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t valueKind, float floatVal,
                    int64_t intVal, uint8_t restart,
                    const char* desc) {
        C::Entry e;
        e.configId = id; e.name = name; e.description = desc;
        e.configKind = C::Performance;
        e.valueKind = valueKind;
        e.floatValue = floatVal;
        e.intValue = intVal;
        e.restartRequired = restart;
        e.iconColorRGBA = packRgba(140, 200, 255);   // perf blue
        c.entries.push_back(e);
    };
    add(100, "MaxCreaturesPerCell", C::Int, 0.0f, 100, 1,
        "Maximum creatures spawned per 533x533yd cell. "
        "Higher = denser zones but more memory + more "
        "AI updates per server tick. 100 is the canonical "
        "Wrath-era default. Restart REQUIRED.");
    add(101, "DefaultViewDistance", C::Float, 533.0f, 0, 0,
        "Default client view distance in yards. 533 = "
        "1 cell width - clients can see one cell beyond "
        "their own. Higher values stress server; lower "
        "values reduce load but reduce immersion.");
    add(102, "SpawnRateMultiplier", C::Float, 1.0f, 0, 0,
        "Creature spawn-rate multiplier. 1.0 = "
        "Blizzard default. 2.0 doubles all respawn "
        "rates - useful for low-pop farming zones.");
    add(103, "GCIntervalSec", C::Int, 0.0f, 300, 1,
        "Server-side garbage collection interval in "
        "seconds. 300 (5min) is standard. Lower = more "
        "frequent GC pauses but lower memory peaks. "
        "Restart REQUIRED to apply.");
    return c;
}

WoweeServerConfig WoweeServerConfigLoader::makeSecurity(
    const std::string& catalogName) {
    using C = WoweeServerConfig;
    WoweeServerConfig c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t valueKind, float floatVal,
                    int64_t intVal, const char* strVal,
                    uint8_t restart, const char* desc) {
        C::Entry e;
        e.configId = id; e.name = name; e.description = desc;
        e.configKind = C::Security;
        e.valueKind = valueKind;
        e.floatValue = floatVal;
        e.intValue = intVal;
        e.strValue = strVal;
        e.restartRequired = restart;
        e.iconColorRGBA = packRgba(220, 60, 60);   // security red
        c.entries.push_back(e);
    };
    add(200, "SpeedhackTolerance", C::Float, 1.05f, 0, "", 0,
        "Speed-hack tolerance multiplier. Server "
        "rejects movements > (max_legitimate_speed * "
        "this). 1.05 = 5%% leeway for network jitter. "
        "Lower = tighter detection (more false positives); "
        "higher = looser (more false negatives).");
    add(201, "TradeGoldCapCopper", C::Int, 0.0f, 1000000000, "", 0,
        "Trade-window gold cap in copper. 1000000000 "
        "(100,000g) is the Wrath default. Values > 4 "
        "billion don't fit in uint32, so the field is "
        "int64 to be safe.");
    add(202, "GMAuditLogEnabled", C::Bool, 0.0f, 1, "", 1,
        "Enable GM-command audit logging. When true "
        "(int=1), every GM command is logged to the "
        "audit table for moderation review. Restart "
        "REQUIRED.");
    add(203, "CheatDetectionSensitivity", C::String, 0.0f, 0,
        "high", 0,
        "Cheat-detection sensitivity preset name - "
        "valid: \"low\" / \"medium\" / \"high\" / "
        "\"paranoid\". This is a STRING value (the "
        "first format using valueKind=String) since "
        "the underlying detection logic is named-preset "
        "based, not numeric.");
    return c;
}

} // namespace pipeline
} // namespace wowee
