#include "pipeline/wowee_creature_resists.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'R', 'E'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcre";

} // namespace

const WoweeCreatureResists::Entry*
WoweeCreatureResists::findById(uint32_t resistId) const {
    for (const auto& e : entries)
        if (e.resistId == resistId) return &e;
    return nullptr;
}

const WoweeCreatureResists::Entry*
WoweeCreatureResists::findByCreature(uint32_t creatureEntry) const {
    for (const auto& e : entries)
        if (e.creatureEntry == creatureEntry) return &e;
    return nullptr;
}

bool WoweeCreatureResistsLoader::save(const WoweeCreatureResists& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCreatureResists::Entry& e) {
        writePOD(os, e.resistId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.creatureEntry);
        writePOD(os, e.holyResist);
        writePOD(os, e.fireResist);
        writePOD(os, e.natureResist);
        writePOD(os, e.frostResist);
        writePOD(os, e.shadowResist);
        writePOD(os, e.arcaneResist);
        writePOD(os, e.physicalResistPct);
        writePOD(os, e.pad0);
        writePOD(os, e.ccImmunityMask);
        writePOD(os, e.mechanicImmunityMask);
        writePOD(os, e.schoolImmunityMask);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.pad3);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeCreatureResists WoweeCreatureResistsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCreatureResists>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCreatureResists::Entry& e) {
        if (!readPOD(is, e.resistId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.creatureEntry) ||
            !readPOD(is, e.holyResist) ||
            !readPOD(is, e.fireResist) ||
            !readPOD(is, e.natureResist) ||
            !readPOD(is, e.frostResist) ||
            !readPOD(is, e.shadowResist) ||
            !readPOD(is, e.arcaneResist) ||
            !readPOD(is, e.physicalResistPct) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.ccImmunityMask) ||
            !readPOD(is, e.mechanicImmunityMask) ||
            !readPOD(is, e.schoolImmunityMask) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.pad3) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeCreatureResistsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeCreatureResists WoweeCreatureResistsLoader::makeRaidBosses(
    const std::string& catalogName) {
    using R = WoweeCreatureResists;
    WoweeCreatureResists c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t entry,
                    int16_t holy, int16_t fire,
                    int16_t nature, int16_t frost,
                    int16_t shadow, int16_t arcane,
                    uint8_t physPct,
                    uint16_t ccImm, uint8_t schoolImm,
                    const char* desc) {
        R::Entry e;
        e.resistId = id; e.name = name; e.description = desc;
        e.creatureEntry = entry;
        e.holyResist = holy; e.fireResist = fire;
        e.natureResist = nature; e.frostResist = frost;
        e.shadowResist = shadow; e.arcaneResist = arcane;
        e.physicalResistPct = physPct;
        e.ccImmunityMask = ccImm;
        e.schoolImmunityMask = schoolImm;
        // Bosses immune to most CC by convention (server-
        // wide behavior - bosses can't be CC'd at all).
        e.mechanicImmunityMask = 0xFFFFFFFFu;
        e.iconColorRGBA = packRgba(220, 60, 60);   // boss red
        c.entries.push_back(e);
    };
    // Ragnaros - fire-school immune (school bit 0x04 in
    // typical school-bit numbering: holy=1, fire=2,
    // nature=4, frost=8, shadow=16, arcane=32). Using
    // 0x02 here for fire.
    add(1, "RagnarosFireImmune", 11502,
        0, 32767, 0, 0, 0, 0, 0,
        0xFFFF, 0x02,
        "Ragnaros (Molten Core boss) - 100% fire-school "
        "immunity (fireResist=32767 = full block) plus "
        "schoolImmunityMask bit for fire. All CC immune.");
    add(2, "VaelHalfResist", 13020,
        100, 100, 100, 100, 100, 100, 0,
        0xFFFF, 0,
        "Vaelastrasz (BWL) - 100 resist to all 6 magic "
        "schools (~50% mitigation against caster spells). "
        "All CC immune.");
    add(3, "HakkarArcaneImmune", 14834,
        0, 0, 0, 0, 0, 32767, 0,
        0xFFFF, 0x20,
        "Hakkar (ZG) - full arcane immunity. All CC "
        "immune. Other schools at default zero resist.");
    add(4, "KelthuzadShadowImmune", 15990,
        0, 0, 0, 200, 32767, 0, 0,
        0xFFFF, 0x10,
        "Kel'Thuzad (Naxx) - full shadow-school "
        "immunity, plus 200 frost resist (~50% frost "
        "mitigation). Iconic anti-warlock fight.");
    add(5, "OnyxiaPartialImmune", 10184,
        0, 100, 0, 100, 0, 0, 0,
        0xFFFF, 0,
        "Onyxia - 100 fire + 100 frost resist (50% "
        "mitigation against both schools). All CC "
        "immune.");
    return c;
}

WoweeCreatureResists WoweeCreatureResistsLoader::makeElites(
    const std::string& catalogName) {
    using R = WoweeCreatureResists;
    WoweeCreatureResists c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t entry,
                    int16_t holy, int16_t fire,
                    int16_t nature, int16_t frost,
                    int16_t shadow, int16_t arcane,
                    const char* desc) {
        R::Entry e;
        e.resistId = id; e.name = name; e.description = desc;
        e.creatureEntry = entry;
        e.holyResist = holy; e.fireResist = fire;
        e.natureResist = nature; e.frostResist = frost;
        e.shadowResist = shadow; e.arcaneResist = arcane;
        e.physicalResistPct = 0;
        // Elites can be CC'd but not all - 80%
        // mechanicImmune to common debuffs.
        e.ccImmunityMask = R::ImmuneFear | R::ImmuneSleep;
        e.mechanicImmunityMask = 0;
        e.schoolImmunityMask = 0;
        e.iconColorRGBA = packRgba(200, 140, 60);   // elite gold
        c.entries.push_back(e);
    };
    add(100, "WaterElementalFireResist", 12471,
        0, 60, 0, 0, 0, 0,
        "Water Elemental (Bog of Sorrows) - 60 fire "
        "resist (~30% mitigation). Frost-themed mob "
        "resists fire by lore.");
    add(101, "StoneGiantNatureResist", 12476,
        0, 0, 100, 0, 0, 0,
        "Stone Giant (Stonetalon) - 100 nature resist "
        "(50% mitigation). Earth-element mob resists "
        "druid spells.");
    add(102, "ScarletPriestHolyResist", 11030,
        80, 0, 0, 0, 60, 0,
        "Scarlet Crusade Priest (Scarlet Monastery) - "
        "80 holy + 60 shadow resist. Light/dark-school "
        "trained.");
    add(103, "DustwindStormcasterArcane", 8519,
        0, 0, 0, 0, 0, 80,
        "Dustwind Stormcaster (Silithus) - 80 arcane "
        "resist (40% mitigation). Caster mob favors "
        "arcane defense.");
    add(104, "FrostwolfEntinelFrost", 10920,
        0, 0, 0, 60, 0, 0,
        "Frostwolf Sentinel (Alterac Valley) - 60 frost "
        "resist (30% mitigation). Northern enemy "
        "acclimated to cold.");
    return c;
}

WoweeCreatureResists WoweeCreatureResistsLoader::makeImmunities(
    const std::string& catalogName) {
    using R = WoweeCreatureResists;
    WoweeCreatureResists c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t entry, uint16_t ccMask,
                    uint32_t mechMask, const char* desc) {
        R::Entry e;
        e.resistId = id; e.name = name; e.description = desc;
        e.creatureEntry = entry;
        e.ccImmunityMask = ccMask;
        e.mechanicImmunityMask = mechMask;
        e.schoolImmunityMask = 0;
        e.iconColorRGBA = packRgba(140, 100, 240);   // immune purple
        c.entries.push_back(e);
    };
    add(200, "RootImmuneTreant",   12477,
        R::ImmuneRoot | R::ImmuneSnare, 0,
        "Treant (Felwood) - root + snare immune. "
        "Cannot be Entangling Roots or Frost Trap'd.");
    add(201, "StunImmuneAlphaWolf", 14283,
        R::ImmuneStun, 0,
        "Alpha Worg (Silverpine) - stun immune. "
        "Cannot be Cheap Shot'd or Concussion Blow'd.");
    add(202, "SilenceImmuneCaster", 11839,
        R::ImmuneSilence | R::ImmuneInterrupt, 0,
        "Cult of the Damned Acolyte - silence + "
        "interrupt immune. Spell pushback works but "
        "the cast itself can't be interrupted.");
    add(203, "FearImmuneUndead",    18525,
        R::ImmuneFear | R::ImmuneCharm | R::ImmunePolymorph,
        0,
        "Risen Construct (Naxxramas) - fear + charm + "
        "polymorph immune. Mind-affecting CC has no "
        "effect; physical CC like stun/root still works.");
    return c;
}

} // namespace pipeline
} // namespace wowee
