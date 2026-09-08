#include "pipeline/wowee_sky_params.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'K', 'P'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wskp";

} // namespace

const WoweeSkyParams::Entry*
WoweeSkyParams::findById(uint32_t skyId) const {
    for (const auto& e : entries)
        if (e.skyId == skyId) return &e;
    return nullptr;
}

std::vector<const WoweeSkyParams::Entry*>
WoweeSkyParams::findByArea(uint32_t mapId,
                              uint32_t areaId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.mapId == mapId && e.areaId == areaId)
            out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->timeOfDayHour < b->timeOfDayHour;
              });
    return out;
}

bool WoweeSkyParamsLoader::save(const WoweeSkyParams& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSkyParams::Entry& e) {
        writePOD(os, e.skyId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.areaId);
        writePOD(os, e.timeOfDayHour);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.zenithColor);
        writePOD(os, e.horizonColor);
        writePOD(os, e.sunColor);
        writePOD(os, e.sunAngleDeg);
        writePOD(os, e.fogStartYards);
        writePOD(os, e.fogEndYards);
        writePOD(os, e.cloudOpacity);
        writePOD(os, e.cloudSpeedX10);
        writePOD(os, e.pad3);
        writePOD(os, e.pad4);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSkyParams WoweeSkyParamsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSkyParams>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSkyParams::Entry& e) {
        if (!readPOD(is, e.skyId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.areaId) ||
            !readPOD(is, e.timeOfDayHour) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.zenithColor) ||
            !readPOD(is, e.horizonColor) ||
            !readPOD(is, e.sunColor) ||
            !readPOD(is, e.sunAngleDeg) ||
            !readPOD(is, e.fogStartYards) ||
            !readPOD(is, e.fogEndYards) ||
            !readPOD(is, e.cloudOpacity) ||
            !readPOD(is, e.cloudSpeedX10) ||
            !readPOD(is, e.pad3) ||
            !readPOD(is, e.pad4) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSkyParamsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSkyParams WoweeSkyParamsLoader::makeStormwindDay(
    const std::string& catalogName) {
    using S = WoweeSkyParams;
    WoweeSkyParams c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t hour,
                    uint32_t zenith, uint32_t horizon,
                    uint32_t sun, float sunAngle,
                    float fogS, float fogE,
                    uint8_t cloudO, uint8_t cloudSp,
                    const char* desc) {
        S::Entry e;
        e.skyId = id; e.name = name; e.description = desc;
        e.mapId = 0;            // Eastern Kingdoms
        e.areaId = 1519;        // Stormwind City
        e.timeOfDayHour = hour;
        e.zenithColor = zenith;
        e.horizonColor = horizon;
        e.sunColor = sun;
        e.sunAngleDeg = sunAngle;
        e.fogStartYards = fogS;
        e.fogEndYards = fogE;
        e.cloudOpacity = cloudO;
        e.cloudSpeedX10 = cloudSp;
        e.iconColorRGBA = packRgba(140, 200, 255);   // sky blue
        c.entries.push_back(e);
    };
    add(1, "StormwindDawn",   6,
        packRgba( 80, 100, 180),     // dawn lavender
        packRgba(220, 160, 100),     // peach horizon
        packRgba(255, 220, 180),     // warm sun
        90.0f, 80.0f, 600.0f, 100, 25,
        "Stormwind 6AM - sun at horizon (90deg "
        "azimuth). Lavender zenith fading to peach. "
        "Light morning fog at 80yd.");
    add(2, "StormwindNoon",  12,
        packRgba( 60, 130, 220),     // bright blue
        packRgba(180, 210, 240),     // pale horizon
        packRgba(255, 250, 230),     // bright sun
        180.0f, 200.0f, 800.0f, 80, 30,
        "Stormwind noon - sun overhead (180deg). "
        "Bright cyan-blue zenith, faint cloud layer.");
    add(3, "StormwindDusk",  18,
        packRgba(140, 100, 180),     // dusk purple
        packRgba(240, 140,  60),     // orange horizon
        packRgba(255, 180, 100),     // sunset sun
        270.0f, 70.0f, 500.0f, 120, 35,
        "Stormwind 6PM - sun setting at western "
        "horizon. Purple zenith fading to orange. "
        "Slightly heavier cloud layer.");
    add(4, "StormwindMidnight", 0,
        packRgba( 10,  20,  60),     // deep blue-black
        packRgba( 30,  40,  80),     // navy horizon
        packRgba(180, 180, 220),     // moon
        180.0f, 60.0f, 400.0f, 60, 20,
        "Stormwind midnight - moon at zenith. "
        "Deep blue-black sky, short fog distance for "
        "intimate night feel.");
    return c;
}

WoweeSkyParams WoweeSkyParamsLoader::makeNorthrendArctic(
    const std::string& catalogName) {
    using S = WoweeSkyParams;
    WoweeSkyParams c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t hour,
                    uint32_t zenith, uint32_t horizon,
                    uint32_t sun, float sunAngle,
                    float fogS, float fogE,
                    uint8_t cloudO, uint8_t cloudSp,
                    const char* desc) {
        S::Entry e;
        e.skyId = id; e.name = name; e.description = desc;
        e.mapId = 571;          // Northrend
        e.areaId = 4395;        // Dalaran (placeholder
                                 // for arctic zone)
        e.timeOfDayHour = hour;
        e.zenithColor = zenith;
        e.horizonColor = horizon;
        e.sunColor = sun;
        e.sunAngleDeg = sunAngle;
        e.fogStartYards = fogS;
        e.fogEndYards = fogE;
        e.cloudOpacity = cloudO;
        e.cloudSpeedX10 = cloudSp;
        e.iconColorRGBA = packRgba(180, 220, 240);   // arctic ice
        c.entries.push_back(e);
    };
    add(100, "ArcticDawn",  6,
        packRgba(140, 180, 220),
        packRgba(220, 200, 220),
        packRgba(220, 230, 240),
        90.0f, 50.0f, 350.0f, 200, 70,
        "Arctic 6AM - pale steel-blue with weak peachy "
        "horizon. Dense ice-fog at 50yd. Strong wind "
        "(7mph cloud drift).");
    add(101, "ArcticNoon", 12,
        packRgba(160, 200, 240),
        packRgba(200, 220, 240),
        packRgba(255, 255, 240),
        180.0f, 100.0f, 500.0f, 180, 60,
        "Arctic noon - bright but flat steel-blue sky. "
        "Snow glare from sun.");
    add(102, "ArcticDusk", 18,
        packRgba(100, 130, 180),
        packRgba(180, 140, 160),
        packRgba(220, 200, 200),
        270.0f, 40.0f, 300.0f, 220, 80,
        "Arctic 6PM - pale violet zenith with washed-"
        "rose horizon. Maximum fog density (300yd).");
    add(103, "ArcticMidnight", 0,
        packRgba( 20,  30,  60),
        packRgba( 40,  60,  80),
        packRgba(200, 220, 240),
        180.0f, 30.0f, 250.0f, 240, 50,
        "Arctic midnight - near-pitch dark with cold "
        "moon. Minimum fog visibility (30yd start) - "
        "blizzard-style whiteout.");
    return c;
}

WoweeSkyParams WoweeSkyParamsLoader::makeOutlandHellfire(
    const std::string& catalogName) {
    using S = WoweeSkyParams;
    WoweeSkyParams c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t hour,
                    uint32_t zenith, uint32_t horizon,
                    uint32_t sun, float sunAngle,
                    float fogS, float fogE,
                    uint8_t cloudO, uint8_t cloudSp,
                    const char* desc) {
        S::Entry e;
        e.skyId = id; e.name = name; e.description = desc;
        e.mapId = 530;          // Outland
        e.areaId = 3483;        // Hellfire Peninsula
        e.timeOfDayHour = hour;
        e.zenithColor = zenith;
        e.horizonColor = horizon;
        e.sunColor = sun;
        e.sunAngleDeg = sunAngle;
        e.fogStartYards = fogS;
        e.fogEndYards = fogE;
        e.cloudOpacity = cloudO;
        e.cloudSpeedX10 = cloudSp;
        e.iconColorRGBA = packRgba(220, 80, 60);   // hellfire red
        c.entries.push_back(e);
    };
    add(200, "OutlandDawn",   6,
        packRgba(180,  60,  60),     // crimson zenith
        packRgba(240, 120,  60),     // orange horizon
        packRgba(255, 200, 100),
        90.0f, 250.0f, 1200.0f, 120, 40,
        "Hellfire 6AM - sky is permanently smoke-tinged. "
        "Crimson zenith with orange horizon. Long sight "
        "distance (1200yd) - Outland is sparse.");
    add(201, "OutlandNoon",  12,
        packRgba(200, 100,  80),
        packRgba(220, 160, 100),
        packRgba(255, 230, 180),
        180.0f, 300.0f, 1500.0f, 100, 35,
        "Hellfire noon - peak orange sky, bright sun "
        "with maximum visibility.");
    add(202, "OutlandSunset", 18,
        packRgba(220,  80,  60),     // scarlet
        packRgba(240, 100,  40),     // deep orange
        packRgba(255, 160, 100),
        270.0f, 200.0f, 1000.0f, 140, 45,
        "Hellfire sunset - most dramatic time, sky "
        "fully scarlet. No midnight keyframe - Outland "
        "is permanently lit by the gravitational anomaly "
        "of the Twisting Nether visible at zenith.");
    return c;
}

} // namespace pipeline
} // namespace wowee
