#include "pipeline/wowee_glyphs.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'L', 'Y'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wgly";

} // namespace

const WoweeGlyph::Entry*
WoweeGlyph::findById(uint32_t glyphId) const {
    for (const auto& e : entries) if (e.glyphId == glyphId) return &e;
    return nullptr;
}

const char* WoweeGlyph::glyphTypeName(uint8_t t) {
    switch (t) {
        case Major: return "major";
        case Minor: return "minor";
        case Prime: return "prime";
        default:    return "unknown";
    }
}

bool WoweeGlyphLoader::save(const WoweeGlyph& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeGlyph::Entry& e) {
        writePOD(os, e.glyphId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.glyphType);
        writePadding(os, 3);
        writePOD(os, e.spellId);
        writePOD(os, e.itemId);
        writePOD(os, e.classMask);
        writePOD(os, e.requiredLevel);
        writePadding(os, 2);
                       });
}

WoweeGlyph WoweeGlyphLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeGlyph>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeGlyph::Entry& e) {
        if (!readPOD(is, e.glyphId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.glyphType)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.spellId) ||
            !readPOD(is, e.itemId) ||
            !readPOD(is, e.classMask) ||
            !readPOD(is, e.requiredLevel)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
                                  return true;
                              });
}

bool WoweeGlyphLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeGlyph WoweeGlyphLoader::makeStarter(const std::string& catalogName) {
    WoweeGlyph c;
    c.name = catalogName;
    {
        WoweeGlyph::Entry e;
        e.glyphId = 1; e.name = "Glyph of Cleaving";
        e.description = "Increases Cleave target count by one.";
        e.iconPath = "Interface/Icons/Inv_Glyph_MajorWarrior.blp";
        e.glyphType = WoweeGlyph::Major;
        e.spellId = 56308;        // canonical WotLK spellId
        e.itemId  = 43412;        // canonical inscription item
        e.classMask = WoweeGlyph::kClassWarrior;
        e.requiredLevel = 25;
        c.entries.push_back(e);
    }
    {
        WoweeGlyph::Entry e;
        e.glyphId = 2; e.name = "Glyph of Polymorph";
        e.description = "Polymorph cleanses DoT effects on the target.";
        e.iconPath = "Interface/Icons/Inv_Glyph_MajorMage.blp";
        e.glyphType = WoweeGlyph::Major;
        e.spellId = 56375;
        e.itemId  = 42735;
        e.classMask = WoweeGlyph::kClassMage;
        e.requiredLevel = 25;
        c.entries.push_back(e);
    }
    {
        WoweeGlyph::Entry e;
        e.glyphId = 3; e.name = "Glyph of Vanish";
        e.description = "Vanish increases your run speed for 6 sec.";
        e.iconPath = "Interface/Icons/Inv_Glyph_MajorRogue.blp";
        e.glyphType = WoweeGlyph::Major;
        e.spellId = 56474;
        e.itemId  = 42964;
        e.classMask = WoweeGlyph::kClassRogue;
        e.requiredLevel = 25;
        c.entries.push_back(e);
    }
    return c;
}

WoweeGlyph WoweeGlyphLoader::makeWarrior(const std::string& catalogName) {
    WoweeGlyph c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    uint32_t spellId, uint32_t itemId,
                    uint16_t reqLevel, const char* desc) {
        WoweeGlyph::Entry e;
        e.glyphId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_Glyph_") +
                      (type == WoweeGlyph::Major ? "MajorWarrior" : "MinorWarrior") +
                      ".blp";
        e.glyphType = type;
        e.spellId = spellId; e.itemId = itemId;
        e.classMask = WoweeGlyph::kClassWarrior;
        e.requiredLevel = reqLevel;
        c.entries.push_back(e);
    };
    // 3 majors.
    add(100, "Glyph of Cleaving",       WoweeGlyph::Major,
        56308, 43412, 25, "Cleave hits +1 target.");
    add(101, "Glyph of Bloodthirst",    WoweeGlyph::Major,
        58368, 43395, 50, "Bloodthirst heals you for additional health.");
    add(102, "Glyph of Heroic Strike",  WoweeGlyph::Major,
        58367, 43399, 70, "Heroic Strike refunds rage on critical hits.");
    // 3 minors.
    add(110, "Glyph of Battle",         WoweeGlyph::Minor,
        58095, 43395, 25, "Battle Shout duration increased by 30 min.");
    add(111, "Glyph of Charge",         WoweeGlyph::Minor,
        58097, 43413, 50, "Charge range increased by 5 yards.");
    add(112, "Glyph of Mocking Blow",   WoweeGlyph::Minor,
        58099, 43421, 70, "Mocking Blow taunt duration extended by 4 sec.");
    return c;
}

WoweeGlyph WoweeGlyphLoader::makeUniversal(const std::string& catalogName) {
    WoweeGlyph c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    uint32_t spellId, uint32_t itemId,
                    uint16_t reqLevel, const char* desc) {
        WoweeGlyph::Entry e;
        e.glyphId = id; e.name = name; e.description = desc;
        e.iconPath = "Interface/Icons/Inv_Glyph_Generic.blp";
        e.glyphType = type;
        e.spellId = spellId; e.itemId = itemId;
        e.classMask = WoweeGlyph::kClassAll;   // any class
        e.requiredLevel = reqLevel;
        c.entries.push_back(e);
    };
    add(200, "Glyph of Hearthstone",      WoweeGlyph::Minor,
        59672, 43906, 25,
        "Reduces Hearthstone cooldown by 5 minutes.");
    add(201, "Glyph of Levitate",         WoweeGlyph::Minor,
        59673, 43911, 50,
        "Levitate also makes you weightless underwater.");
    add(202, "Glyph of Mounting",         WoweeGlyph::Minor,
        59674, 43912, 50,
        "Reduces mount cast time by 1 second.");
    add(203, "Glyph of Salvation",        WoweeGlyph::Major,
        59675, 43913, 70,
        "Once per minute, automatically dispels a fear effect.");
    return c;
}

} // namespace pipeline
} // namespace wowee
