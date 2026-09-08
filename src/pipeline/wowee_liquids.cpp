#include "pipeline/wowee_liquids.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'I', 'Q'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wliq";

} // namespace

const WoweeLiquid::Entry*
WoweeLiquid::findById(uint32_t liquidId) const {
    for (const auto& e : entries) if (e.liquidId == liquidId) return &e;
    return nullptr;
}

const char* WoweeLiquid::liquidKindName(uint8_t k) {
    switch (k) {
        case Water:         return "water";
        case Magma:         return "magma";
        case Slime:         return "slime";
        case OceanSalt:     return "ocean";
        case FelFire:       return "fel-fire";
        case HolyLight:     return "holy-light";
        case TarOil:        return "tar";
        case AcidBog:       return "acid";
        case FrozenWater:   return "frozen";
        case UnderworldGoo: return "underworld";
        default:            return "unknown";
    }
}

bool WoweeLiquidLoader::save(const WoweeLiquid& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeLiquid::Entry& e) {
        writePOD(os, e.liquidId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.shaderPath);
        writeStr(os, e.materialPath);
        writePOD(os, e.liquidKind);
        writePOD(os, e.fogColorR);
        writePOD(os, e.fogColorG);
        writePOD(os, e.fogColorB);
        writePOD(os, e.fogDensity);
        writePOD(os, e.ambientSoundId);
        writePOD(os, e.splashSoundId);
        writePOD(os, e.damageSpellId);
        writePOD(os, e.damagePerSecond);
        writePOD(os, e.minimapColor);
        writePOD(os, e.flowDirection);
        writePOD(os, e.flowSpeed);
        writePOD(os, e.viscosity);
                       });
}

WoweeLiquid WoweeLiquidLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeLiquid>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeLiquid::Entry& e) {
        if (!readPOD(is, e.liquidId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.shaderPath) ||
            !readStr(is, e.materialPath)) { return false; }
        if (!readPOD(is, e.liquidKind) ||
            !readPOD(is, e.fogColorR) ||
            !readPOD(is, e.fogColorG) ||
            !readPOD(is, e.fogColorB) ||
            !readPOD(is, e.fogDensity) ||
            !readPOD(is, e.ambientSoundId) ||
            !readPOD(is, e.splashSoundId) ||
            !readPOD(is, e.damageSpellId) ||
            !readPOD(is, e.damagePerSecond) ||
            !readPOD(is, e.minimapColor) ||
            !readPOD(is, e.flowDirection) ||
            !readPOD(is, e.flowSpeed) ||
            !readPOD(is, e.viscosity)) { return false; }
                                  return true;
                              });
}

bool WoweeLiquidLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeLiquid WoweeLiquidLoader::makeStarter(const std::string& catalogName) {
    WoweeLiquid c;
    c.name = catalogName;
    {
        WoweeLiquid::Entry e;
        e.liquidId = 1; e.name = "Fresh Water";
        e.description = "Standard rivers, lakes, and ponds.";
        e.shaderPath = "shaders/water_basic.frag";
        e.materialPath = "textures/liquid/water_array.dds";
        e.liquidKind = WoweeLiquid::Water;
        e.fogColorR = 80; e.fogColorG = 130; e.fogColorB = 180;
        e.fogDensity = 0.04f;
        e.minimapColor = packRgba(80, 130, 180);
        e.flowSpeed = 0.5f;
        e.ambientSoundId = 10;       // hypothetical WSND id
        e.splashSoundId = 11;
        c.entries.push_back(e);
    }
    {
        WoweeLiquid::Entry e;
        e.liquidId = 2; e.name = "Lava";
        e.description = "Burning magma - applies fire DoT to "
                         "anyone who enters.";
        e.shaderPath = "shaders/water_emissive.frag";
        e.materialPath = "textures/liquid/lava_array.dds";
        e.liquidKind = WoweeLiquid::Magma;
        e.fogColorR = 180; e.fogColorG = 60; e.fogColorB = 10;
        e.fogDensity = 0.8f;
        e.minimapColor = packRgba(220, 70, 0);
        e.viscosity = 0.5f;
        e.flowSpeed = 0.05f;
        e.damagePerSecond = 500;
        // 16455 is "Lava" in Spell.dbc. This was 24858, which is Moonkin Form.
        e.damageSpellId = 16455;
        e.ambientSoundId = 12;
        e.splashSoundId = 13;
        c.entries.push_back(e);
    }
    {
        WoweeLiquid::Entry e;
        e.liquidId = 3; e.name = "Sludge Slime";
        e.description = "Toxic green slime found in dungeons.";
        e.shaderPath = "shaders/water_toxic.frag";
        e.materialPath = "textures/liquid/slime_array.dds";
        e.liquidKind = WoweeLiquid::Slime;
        e.fogColorR = 60; e.fogColorG = 130; e.fogColorB = 30;
        e.fogDensity = 0.5f;
        e.minimapColor = packRgba(60, 140, 30);
        e.viscosity = 0.7f;
        e.flowSpeed = 0.1f;
        e.damagePerSecond = 50;
        e.ambientSoundId = 14;
        e.splashSoundId = 15;
        c.entries.push_back(e);
    }
    return c;
}

WoweeLiquid WoweeLiquidLoader::makeMagical(const std::string& catalogName) {
    WoweeLiquid c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t r, uint8_t g, uint8_t b,
                    float density, float visc, uint32_t spellId,
                    const char* desc) {
        WoweeLiquid::Entry e;
        e.liquidId = id; e.name = name; e.description = desc;
        e.shaderPath = "shaders/water_magical.frag";
        e.materialPath = std::string("textures/liquid/") +
                          name + "_array.dds";
        e.liquidKind = kind;
        e.fogColorR = r; e.fogColorG = g; e.fogColorB = b;
        e.fogDensity = density;
        e.minimapColor = packRgba(r, g, b);
        e.viscosity = visc;
        e.damageSpellId = spellId;
        c.entries.push_back(e);
    };
    add(100, "FelFire",   WoweeLiquid::FelFire,
        80, 200, 30, 0.7f, 0.4f, 22682,
        "Demonic green fel - burns even fire-immune creatures.");
    add(101, "HolyLight", WoweeLiquid::HolyLight,
        240, 230, 180, 0.3f, 0.0f, 0,
        "Pool of liquid Light - heals players who enter.");
    add(102, "Underworld", WoweeLiquid::UnderworldGoo,
        70, 30, 100, 0.9f, 0.8f, 27654,
        "Shadow-tainted void liquid - drains mana on contact.");
    add(103, "Cosmic",    WoweeLiquid::HolyLight,
        100, 80, 200, 0.4f, 0.0f, 0,
        "Naaru-touched water - randomly grants buffs.");
    return c;
}

WoweeLiquid WoweeLiquidLoader::makeHazardous(const std::string& catalogName) {
    WoweeLiquid c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t spellId, uint32_t dps,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeLiquid::Entry e;
        e.liquidId = id; e.name = name; e.description = desc;
        e.shaderPath = "shaders/water_hazardous.frag";
        e.materialPath = std::string("textures/liquid/") +
                          name + "_array.dds";
        e.liquidKind = kind;
        e.damageSpellId = spellId;
        e.damagePerSecond = dps;
        e.fogColorR = r; e.fogColorG = g; e.fogColorB = b;
        e.fogDensity = 0.85f;
        e.viscosity = 0.6f;
        e.minimapColor = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(200, "NaxxSlime",  WoweeLiquid::Slime,
        28157, 1500, 100, 200, 60,
        "Naxxramas-grade plague slime - lethal to non-tanks.");
    add(201, "AcidBog",    WoweeLiquid::AcidBog,
        29213,  300,  90, 160, 40,
        "Greenish acid - destroys armor durability over time.");
    add(202, "FelLava",    WoweeLiquid::Magma,
        30122, 2000, 130, 220, 30,
        "Fel-corrupted lava - applies a stacking burn debuff.");
    return c;
}

} // namespace pipeline
} // namespace wowee
