#include "pipeline/wowee_spell_schools.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'C', 'H'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wsch";

} // namespace

const WoweeSpellSchool::Entry*
WoweeSpellSchool::findById(uint32_t schoolId) const {
    for (const auto& e : entries)
        if (e.schoolId == schoolId) return &e;
    return nullptr;
}

bool WoweeSpellSchoolLoader::save(const WoweeSpellSchool& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeSpellSchool::Entry& e) {
        writePOD(os, e.schoolId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.canBeImmune);
        writePOD(os, e.canBeAbsorbed);
        writePOD(os, e.canBeReflected);
        writePOD(os, e.canCrit);
        writePOD(os, e.colorRGBA);
        writePOD(os, e.baseResistanceCap);
        writePOD(os, e.castSoundId);
        writePOD(os, e.impactSoundId);
        writePOD(os, e.combinedSchoolMask);
                       });
}

WoweeSpellSchool WoweeSpellSchoolLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellSchool>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellSchool::Entry& e) {
        if (!readPOD(is, e.schoolId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.canBeImmune) ||
            !readPOD(is, e.canBeAbsorbed) ||
            !readPOD(is, e.canBeReflected) ||
            !readPOD(is, e.canCrit) ||
            !readPOD(is, e.colorRGBA) ||
            !readPOD(is, e.baseResistanceCap) ||
            !readPOD(is, e.castSoundId) ||
            !readPOD(is, e.impactSoundId) ||
            !readPOD(is, e.combinedSchoolMask)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellSchoolLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeSpellSchool WoweeSpellSchoolLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellSchool c;
    c.name = catalogName;
    {
        WoweeSpellSchool::Entry e;
        e.schoolId = WoweeSpellSchool::kSchoolPhysical;
        e.name = "Physical";
        e.description = "Melee + ranged weapon damage. Mitigated by "
                         "armor instead of resistance.";
        e.iconPath = "Interface/Icons/INV_Sword_04.blp";
        e.canBeAbsorbed = 1;
        e.canBeReflected = 0;     // physical hits aren't reflected
        e.canCrit = 1;
        e.colorRGBA = packRgba(220, 220, 220);  // light gray
        e.baseResistanceCap = 0;  // armor instead
        c.entries.push_back(e);
    }
    {
        WoweeSpellSchool::Entry e;
        e.schoolId = WoweeSpellSchool::kSchoolFire;
        e.name = "Fire";
        e.description = "Pyromancy / dragon breath / molten attacks.";
        e.iconPath = "Interface/Icons/Spell_Fire_FlameBolt.blp";
        e.canBeAbsorbed = 1;
        e.canBeReflected = 1;
        e.canCrit = 1;
        e.colorRGBA = packRgba(220, 70, 0);  // orange-red
        e.baseResistanceCap = 365;
        c.entries.push_back(e);
    }
    {
        WoweeSpellSchool::Entry e;
        e.schoolId = WoweeSpellSchool::kSchoolHoly;
        e.name = "Holy";
        e.description = "Light-aspected damage and healing.";
        e.iconPath = "Interface/Icons/Spell_Holy_HolyBolt.blp";
        e.canBeImmune = 0;        // holy can't be resisted
        e.canBeAbsorbed = 1;
        e.canBeReflected = 0;
        e.canCrit = 1;
        e.colorRGBA = packRgba(255, 230, 130);  // pale gold
        e.baseResistanceCap = 0;
        c.entries.push_back(e);
    }
    return c;
}

WoweeSpellSchool WoweeSpellSchoolLoader::makeMagical(
    const std::string& catalogName) {
    WoweeSpellSchool c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t r,
                    uint8_t g, uint8_t b, uint32_t resCap,
                    const char* desc) {
        WoweeSpellSchool::Entry e;
        e.schoolId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_") +
                      name + ".blp";
        e.colorRGBA = packRgba(r, g, b);
        e.baseResistanceCap = resCap;
        e.canBeAbsorbed = 1;
        e.canBeReflected = 1;
        e.canCrit = 1;
        c.entries.push_back(e);
    };
    add(WoweeSpellSchool::kSchoolHoly,   "Holy",
        255, 230, 130,   0,  // no holy resist gear
        "Light-aspected damage / heal.");
    add(WoweeSpellSchool::kSchoolFire,   "Fire",
        220, 70, 0,      365,
        "Pyromancy / dragon breath.");
    add(WoweeSpellSchool::kSchoolNature, "Nature",
        50, 200, 50,     365,
        "Lightning / poison / wild growth.");
    add(WoweeSpellSchool::kSchoolFrost,  "Frost",
        150, 200, 255,   365,
        "Ice / chill / glacial.");
    add(WoweeSpellSchool::kSchoolShadow, "Shadow",
        90, 30, 130,     365,
        "Necromancy / void / corruption.");
    add(WoweeSpellSchool::kSchoolArcane, "Arcane",
        180, 100, 220,   365,
        "Pure mana / arcane missiles / time.");
    return c;
}

WoweeSpellSchool WoweeSpellSchoolLoader::makeCombined(
    const std::string& catalogName) {
    WoweeSpellSchool c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t maskBits,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellSchool::Entry e;
        e.schoolId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_") +
                      name + ".blp";
        e.combinedSchoolMask = maskBits;
        e.colorRGBA = packRgba(r, g, b);
        e.canBeAbsorbed = 1;
        e.baseResistanceCap = 365;
        c.entries.push_back(e);
    };
    // Hybrid schools - combinedSchoolMask is the bitmask of
    // canonical schools they qualify as. Spell engine uses the
    // LOWER resistance of the combined set, so hybrids bypass
    // single-school resist gear.
    add(0x80000001, "Spellfire",
        WoweeSpellSchool::kSchoolFire | WoweeSpellSchool::kSchoolArcane,
        230, 100, 200,
        "Combined Fire+Arcane - bypasses single-school resist.");
    add(0x80000002, "Spellshadow",
        WoweeSpellSchool::kSchoolShadow | WoweeSpellSchool::kSchoolArcane,
        140, 50, 200,
        "Combined Shadow+Arcane - Shadow priest specialty.");
    add(0x80000003, "Spellfrost",
        WoweeSpellSchool::kSchoolFrost | WoweeSpellSchool::kSchoolArcane,
        130, 180, 240,
        "Combined Frost+Arcane - Frostfire bolt class.");
    return c;
}

} // namespace pipeline
} // namespace wowee
