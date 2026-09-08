#include "pipeline/wowee_raid_markers.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'A', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wmar";

} // namespace

const WoweeRaidMarkers::Entry*
WoweeRaidMarkers::findById(uint32_t markerId) const {
    for (const auto& e : entries)
        if (e.markerId == markerId) return &e;
    return nullptr;
}

std::vector<const WoweeRaidMarkers::Entry*>
WoweeRaidMarkers::findByKind(uint8_t markerKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.markerKind == markerKind) out.push_back(&e);
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->priority < b->priority;
              });
    return out;
}

bool WoweeRaidMarkersLoader::save(const WoweeRaidMarkers& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeRaidMarkers::Entry& e) {
        writePOD(os, e.markerId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.markerKind);
        writePOD(os, e.priority);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writeStr(os, e.iconPath);
        writeStr(os, e.displayChar);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeRaidMarkers WoweeRaidMarkersLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeRaidMarkers>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeRaidMarkers::Entry& e) {
        if (!readPOD(is, e.markerId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.markerKind) ||
            !readPOD(is, e.priority) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1)) { return false; }
        if (!readStr(is, e.iconPath) ||
            !readStr(is, e.displayChar)) { return false; }
        if (!readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeRaidMarkersLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeRaidMarkers WoweeRaidMarkersLoader::makeRaidTargets(
    const std::string& catalogName) {
    using M = WoweeRaidMarkers;
    WoweeRaidMarkers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t prio, const char* iconPath,
                    const char* glyph, uint32_t color,
                    const char* desc) {
        M::Entry e;
        e.markerId = id; e.name = name; e.description = desc;
        e.markerKind = M::RaidTarget;
        e.priority = prio;
        e.iconPath = iconPath;
        e.displayChar = glyph;
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    // Canonical 8-marker order from /raidicon command.
    add(1, "Star", 0,
        "Interface\\TargetingFrame\\UI-RaidTargetingIcon_1.blp",
        "*", packRgba(255, 220, 80),
        "Star (yellow). Mark slot 1. /raidicon star.");
    add(2, "Circle", 1,
        "Interface\\TargetingFrame\\UI-RaidTargetingIcon_2.blp",
        "o", packRgba(255, 130, 60),
        "Circle (orange). Mark slot 2.");
    add(3, "Diamond", 2,
        "Interface\\TargetingFrame\\UI-RaidTargetingIcon_3.blp",
        "<>", packRgba(180, 100, 240),
        "Diamond (purple). Mark slot 3 - common "
        "polymorph CC-target marker.");
    add(4, "Triangle", 3,
        "Interface\\TargetingFrame\\UI-RaidTargetingIcon_4.blp",
        "^", packRgba(80, 220, 80),
        "Triangle (green). Mark slot 4.");
    add(5, "Moon", 4,
        "Interface\\TargetingFrame\\UI-RaidTargetingIcon_5.blp",
        "(", packRgba(220, 220, 240),
        "Moon (silver). Mark slot 5 - common Sap CC-"
        "target marker.");
    add(6, "Square", 5,
        "Interface\\TargetingFrame\\UI-RaidTargetingIcon_6.blp",
        "#", packRgba(80, 130, 240),
        "Square (blue). Mark slot 6.");
    add(7, "Cross", 6,
        "Interface\\TargetingFrame\\UI-RaidTargetingIcon_7.blp",
        "X", packRgba(220, 60, 60),
        "Cross (red). Mark slot 7 - universal kill-this-"
        "first marker.");
    add(8, "Skull", 7,
        "Interface\\TargetingFrame\\UI-RaidTargetingIcon_8.blp",
        "$", packRgba(220, 220, 220),
        "Skull (white). Mark slot 8 - universal HIGHEST-"
        "priority kill-target marker.");
    return c;
}

WoweeRaidMarkers WoweeRaidMarkersLoader::makeWorldMapPins(
    const std::string& catalogName) {
    using M = WoweeRaidMarkers;
    WoweeRaidMarkers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t prio, const char* iconPath,
                    const char* glyph, uint32_t color,
                    const char* desc) {
        M::Entry e;
        e.markerId = id; e.name = name; e.description = desc;
        e.markerKind = M::WorldMap;
        e.priority = prio;
        e.iconPath = iconPath;
        e.displayChar = glyph;
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    add(100, "Pin", 0,
        "Interface\\Minimap\\WorldMap\\Pin_Yellow.blp",
        "P", packRgba(255, 220, 80),
        "Generic yellow pin. Player-placed waypoint.");
    add(101, "Flag", 1,
        "Interface\\Minimap\\WorldMap\\Flag_Red.blp",
        "F", packRgba(220, 60, 60),
        "Red flag. Used by raid leaders for "
        "rendezvous points.");
    add(102, "Crosshair", 2,
        "Interface\\Minimap\\WorldMap\\Crosshair.blp",
        "+", packRgba(180, 180, 180),
        "Crosshair. Targeting / sniping indicator on "
        "PvP maps.");
    add(103, "Question", 3,
        "Interface\\Minimap\\WorldMap\\Question_Blue.blp",
        "?", packRgba(140, 200, 255),
        "Blue question mark. Quest-related point of "
        "interest.");
    add(104, "Compass", 4,
        "Interface\\Minimap\\WorldMap\\Compass.blp",
        "N", packRgba(220, 220, 240),
        "Compass rose. North-indicator overlay (rare; "
        "minimap usually fixes north at top).");
    return c;
}

WoweeRaidMarkers WoweeRaidMarkersLoader::makeParty(
    const std::string& catalogName) {
    using M = WoweeRaidMarkers;
    WoweeRaidMarkers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t prio, const char* iconPath,
                    const char* glyph, uint32_t color,
                    const char* desc) {
        M::Entry e;
        e.markerId = id; e.name = name; e.description = desc;
        e.markerKind = M::Party;
        e.priority = prio;
        e.iconPath = iconPath;
        e.displayChar = glyph;
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    add(200, "TankRole", 0,
        "Interface\\PartyFrame\\Role_Tank.blp",
        "T", packRgba(60, 100, 200),
        "Tank role icon. Shown in groupfinder + party "
        "frame for tank-specced players.");
    add(201, "HealerRole", 1,
        "Interface\\PartyFrame\\Role_Healer.blp",
        "H", packRgba(80, 200, 80),
        "Healer role icon. Shown for healer-specced "
        "players.");
    add(202, "DamageRole", 2,
        "Interface\\PartyFrame\\Role_Damage.blp",
        "D", packRgba(220, 80, 80),
        "DPS role icon (melee + ranged grouped).");
    add(203, "CasterRole", 3,
        "Interface\\PartyFrame\\Role_Caster.blp",
        "C", packRgba(180, 100, 240),
        "Caster sub-role icon for groupfinder filtering "
        "(distinct from melee DPS in some custom-server "
        "configurations).");
    return c;
}

} // namespace pipeline
} // namespace wowee
