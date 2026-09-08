#include "pipeline/wowee_spell_markers.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'P', 'M'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wspm";

} // namespace

const WoweeSpellMarkers::Entry*
WoweeSpellMarkers::findById(uint32_t markerId) const {
    for (const auto& e : entries)
        if (e.markerId == markerId) return &e;
    return nullptr;
}

const WoweeSpellMarkers::Entry*
WoweeSpellMarkers::findBySpell(uint32_t spellId) const {
    for (const auto& e : entries)
        if (e.spellId == spellId) return &e;
    return nullptr;
}

bool WoweeSpellMarkersLoader::save(const WoweeSpellMarkers& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellMarkers::Entry& e) {
        writePOD(os, e.markerId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.spellId);
        writeStr(os, e.groundTexturePath);
        writePOD(os, e.radius);
        writePOD(os, e.duration);
        writePOD(os, e.tickIntervalMs);
        writePOD(os, e.decalColor);
        writePOD(os, e.edgeFadeMode);
        writePOD(os, e.stackable);
        writePOD(os, e.destroyOnCancel);
        writePOD(os, e.pad0);
        writePOD(os, e.tickSoundId);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeSpellMarkers WoweeSpellMarkersLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellMarkers>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellMarkers::Entry& e) {
        if (!readPOD(is, e.markerId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.spellId) ||
            !readStr(is, e.groundTexturePath) ||
            !readPOD(is, e.radius) ||
            !readPOD(is, e.duration) ||
            !readPOD(is, e.tickIntervalMs) ||
            !readPOD(is, e.decalColor) ||
            !readPOD(is, e.edgeFadeMode) ||
            !readPOD(is, e.stackable) ||
            !readPOD(is, e.destroyOnCancel) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.tickSoundId) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellMarkersLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellMarkers WoweeSpellMarkersLoader::makeMageAoE(
    const std::string& catalogName) {
    using S = WoweeSpellMarkers;
    WoweeSpellMarkers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, const char* texPath,
                    float radius, float duration,
                    uint32_t tickMs, uint32_t color,
                    uint8_t fade, uint32_t soundId,
                    const char* desc) {
        S::Entry e;
        e.markerId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.groundTexturePath = texPath;
        e.radius = radius;
        e.duration = duration;
        e.tickIntervalMs = tickMs;
        e.decalColor = color;
        e.edgeFadeMode = fade;
        e.stackable = 0;
        e.destroyOnCancel = 1;
        e.tickSoundId = soundId;
        e.iconColorRGBA = packRgba(140, 200, 255);   // mage blue
        c.entries.push_back(e);
    };
    add(1, "Blizzard",        10, "Spell\\Blizzard\\BlizzardGround.blp",
        12.0f, 8.0f, 1000, packRgba(180, 220, 255, 200),
        S::SoftEdge, 11075,
        "Mage Blizzard ground decal - 12yd radius, 8s "
        "channel, 1s ticks. Soft-edge fade, blue tint, "
        "wind-howl tick sound.");
    add(2, "Flamestrike",     11, "Spell\\Flamestrike\\FlamestrikeRing.blp",
        8.0f, 8.0f, 1000, packRgba(220, 100, 30, 220),
        S::Pulse, 6580,
        "Mage Flamestrike ground decal - 8yd radius, 8s "
        "lingering DoT after initial impact. Pulse fade "
        "to suggest ongoing flames.");
    add(3, "BlastWaveRing",   13, "Spell\\BlastWave\\ImpactRing.blp",
        10.0f, 0.5f, 0, packRgba(255, 180, 80, 255),
        S::Hard, 0,
        "Mage Blast Wave impact ring - 10yd radius, 0.5s "
        "burst, no ticks (single-instance damage). Hard "
        "edge for sharp shockwave.");
    add(4, "FrostNovaRing",   14, "Spell\\FrostNova\\FreezeRing.blp",
        10.0f, 1.0f, 0, packRgba(180, 220, 255, 180),
        S::SoftEdge, 0,
        "Mage Frost Nova freeze indicator - 10yd radius, "
        "1s visible after cast (the actual root lasts 8s "
        "via the spell aura, the marker is just the visual "
        "cue).");
    return c;
}

WoweeSpellMarkers WoweeSpellMarkersLoader::makeRaidHazards(
    const std::string& catalogName) {
    using S = WoweeSpellMarkers;
    WoweeSpellMarkers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, const char* texPath,
                    float radius, float duration,
                    uint32_t tickMs, uint32_t color,
                    uint8_t fade, const char* desc) {
        S::Entry e;
        e.markerId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.groundTexturePath = texPath;
        e.radius = radius;
        e.duration = duration;
        e.tickIntervalMs = tickMs;
        e.decalColor = color;
        e.edgeFadeMode = fade;
        e.stackable = 0;
        e.destroyOnCancel = 0;        // hazards persist
                                       // until duration expires
        e.tickSoundId = 0;
        e.iconColorRGBA = packRgba(220, 80, 80);   // raid red
        c.entries.push_back(e);
    };
    add(100, "PutricidePoisonPool",   70341,
        "Spell\\Poison\\PoisonPool.blp",
        4.0f, 30.0f, 500, packRgba(80, 220, 60, 200),
        S::Pulse,
        "Putricide poison-pool ground decal - 4yd "
        "radius, 30s, 500ms damage ticks. Pulse fade. "
        "Standing in this is a wipe risk.");
    add(101, "SindragosaFrostTomb",   70106,
        "Spell\\Frost\\IceBlock.blp",
        2.5f, 12.0f, 0, packRgba(180, 220, 255, 220),
        S::Hard,
        "Sindragosa frost-tomb circle - 2.5yd radius, "
        "12s, no ticks (damage is from the ice block aura, "
        "not the marker). Hard edge for clear stay-out "
        "boundary.");
    add(102, "SaurfangBloodFrenzy",   72444,
        "Spell\\Blood\\BloodFrenzy.blp",
        15.0f, 60.0f, 1000, packRgba(220, 60, 60, 180),
        S::SoftEdge,
        "Deathbringer Saurfang Mark of the Fallen "
        "Champion zone - 15yd radius, 60s, 1s healing "
        "ticks for Saurfang. DPS this player or wipe.");
    add(103, "ProfessorPutricideShadow", 70447,
        "Spell\\Shadow\\ShadowPuddle.blp",
        5.0f, 25.0f, 500, packRgba(60, 30, 100, 200),
        S::Pulse,
        "DBS / Putricide phase 2 shadow puddle - 5yd "
        "radius, 25s, 500ms shadow ticks.");
    add(104, "MarrowgarBoneStorm",    70233,
        "Spell\\Bone\\BoneStorm.blp",
        18.0f, 20.0f, 1000, packRgba(220, 220, 200, 180),
        S::Pulse,
        "Lord Marrowgar Bone Storm radius - 18yd, 20s, "
        "1s ticks. Marker tracks Marrowgar himself; "
        "stays during the channel, vanishes when he "
        "stops.");
    return c;
}

WoweeSpellMarkers WoweeSpellMarkersLoader::makeEnvironment(
    const std::string& catalogName) {
    using S = WoweeSpellMarkers;
    WoweeSpellMarkers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, const char* texPath,
                    float radius, float duration,
                    uint32_t tickMs, uint32_t color,
                    uint8_t fade, const char* desc) {
        S::Entry e;
        e.markerId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.groundTexturePath = texPath;
        e.radius = radius;
        e.duration = duration;
        e.tickIntervalMs = tickMs;
        e.decalColor = color;
        e.edgeFadeMode = fade;
        e.stackable = 1;            // environmental effects
                                     // can stack (multiple
                                     // lightning strikes)
        e.destroyOnCancel = 0;      // environment, no caster
                                     // to cancel
        e.tickSoundId = 0;
        e.iconColorRGBA = packRgba(180, 180, 200);   // env neutral
        c.entries.push_back(e);
    };
    add(200, "WintergraspLightning",  60014,
        "Spell\\Weather\\LightningStrike.blp",
        6.0f, 1.0f, 0, packRgba(220, 220, 100, 240),
        S::Hard,
        "Wintergrasp open-world lightning strike - 6yd "
        "blast radius, 1s flash. Hard edge for clear "
        "danger zone.");
    add(201, "SilithusSandstormCone", 38567,
        "Spell\\Weather\\SandstormCone.blp",
        25.0f, 0.0f, 2000, packRgba(220, 200, 140, 100),
        S::SoftEdge,
        "Silithus environmental sandstorm cone - 25yd "
        "radius, no time limit (until weather changes), "
        "2s movement-slow ticks.");
    add(202, "OpenWorldBlizzardZone", 30000,
        "Spell\\Weather\\BlizzardZone.blp",
        40.0f, 0.0f, 5000, packRgba(220, 220, 240, 80),
        S::SoftEdge,
        "Northrend open-world blizzard zone - 40yd "
        "radius, no time limit, 5s minor frost ticks. "
        "Pure visual + ambient gameplay (no real damage "
        "in Wrath; placeholder for storm system).");
    return c;
}

} // namespace pipeline
} // namespace wowee
