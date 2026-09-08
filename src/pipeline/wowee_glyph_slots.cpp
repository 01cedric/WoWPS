#include "pipeline/wowee_glyph_slots.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'F', 'S'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wgfs";

} // namespace

const WoweeGlyphSlot::Entry*
WoweeGlyphSlot::findById(uint32_t slotId) const {
    for (const auto& e : entries)
        if (e.slotId == slotId) return &e;
    return nullptr;
}

bool WoweeGlyphSlot::isUnlockedFor(uint32_t slotId,
                                    uint32_t classBit,
                                    uint8_t characterLevel) const {
    const Entry* e = findById(slotId);
    if (!e) return false;
    if ((e->requiredClassMask & classBit) == 0) return false;
    return characterLevel >= e->minLevelToUnlock;
}

const char* WoweeGlyphSlot::slotKindName(uint8_t k) {
    switch (k) {
        case Major: return "major";
        case Minor: return "minor";
        case Prime: return "prime";
        default:    return "unknown";
    }
}

bool WoweeGlyphSlotLoader::save(const WoweeGlyphSlot& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeGlyphSlot::Entry& e) {
        writePOD(os, e.slotId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.slotKind);
        writePOD(os, e.displayOrder);
        writePOD(os, e.minLevelToUnlock);
        writePOD(os, e.pad0);
        writePOD(os, e.requiredClassMask);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeGlyphSlot WoweeGlyphSlotLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeGlyphSlot>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeGlyphSlot::Entry& e) {
        if (!readPOD(is, e.slotId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.slotKind) ||
            !readPOD(is, e.displayOrder) ||
            !readPOD(is, e.minLevelToUnlock) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.requiredClassMask) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeGlyphSlotLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeGlyphSlot WoweeGlyphSlotLoader::makeStarter(
    const std::string& catalogName) {
    using G = WoweeGlyphSlot;
    WoweeGlyphSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t order, uint8_t lvl,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        G::Entry e;
        e.slotId = id; e.name = name; e.description = desc;
        e.slotKind = kind;
        e.displayOrder = order;
        e.minLevelToUnlock = lvl;
        e.requiredClassMask = 0xFFFFFFFFu;   // all classes
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    // Three Major + three Minor, all-class baseline.
    add(1, "MajorSlot1", G::Major, 0, 25,  240, 200, 100,
        "Major glyph slot 1 - unlocks at level 25.");
    add(2, "MajorSlot2", G::Major, 1, 50,  240, 200, 100,
        "Major glyph slot 2 - unlocks at level 50.");
    add(3, "MajorSlot3", G::Major, 2, 75,  240, 200, 100,
        "Major glyph slot 3 - unlocks at level 75.");
    add(4, "MinorSlot1", G::Minor, 0, 25,  150, 200, 240,
        "Minor glyph slot 1 - unlocks at level 25.");
    add(5, "MinorSlot2", G::Minor, 1, 50,  150, 200, 240,
        "Minor glyph slot 2 - unlocks at level 50.");
    add(6, "MinorSlot3", G::Minor, 2, 75,  150, 200, 240,
        "Minor glyph slot 3 - unlocks at level 75.");
    return c;
}

WoweeGlyphSlot WoweeGlyphSlotLoader::makeWotlk(
    const std::string& catalogName) {
    using G = WoweeGlyphSlot;
    WoweeGlyphSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t order, uint8_t lvl, const char* desc) {
        G::Entry e;
        e.slotId = id; e.name = name; e.description = desc;
        e.slotKind = kind;
        e.displayOrder = order;
        e.minLevelToUnlock = lvl;
        e.requiredClassMask = 0xFFFFFFFFu;
        // Color by kind so the UI distinguishes them.
        e.iconColorRGBA = (kind == G::Major) ? packRgba(240, 200, 100)
                                              : packRgba(150, 200, 240);
        c.entries.push_back(e);
    };
    // WotLK 3.3.5a: 3 Major + 3 Minor with staggered
    // unlocks 15/30/50 and 15/50/70.
    add(100, "WotlkMajor1", G::Major, 0, 15, "Major slot 1 - unlocks at 15.");
    add(101, "WotlkMajor2", G::Major, 1, 30, "Major slot 2 - unlocks at 30.");
    add(102, "WotlkMajor3", G::Major, 2, 50, "Major slot 3 - unlocks at 50.");
    add(103, "WotlkMinor1", G::Minor, 0, 15, "Minor slot 1 - unlocks at 15.");
    add(104, "WotlkMinor2", G::Minor, 1, 50, "Minor slot 2 - unlocks at 50.");
    add(105, "WotlkMinor3", G::Minor, 2, 70, "Minor slot 3 - unlocks at 70.");
    return c;
}

WoweeGlyphSlot WoweeGlyphSlotLoader::makeCata(
    const std::string& catalogName) {
    using G = WoweeGlyphSlot;
    WoweeGlyphSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t order, uint8_t lvl, const char* desc) {
        G::Entry e;
        e.slotId = id; e.name = name; e.description = desc;
        e.slotKind = kind;
        e.displayOrder = order;
        e.minLevelToUnlock = lvl;
        e.requiredClassMask = 0xFFFFFFFFu;
        // Color by kind: prime=red, major=gold, minor=blue.
        if      (kind == G::Prime) e.iconColorRGBA = packRgba(240, 100, 100);
        else if (kind == G::Major) e.iconColorRGBA = packRgba(240, 200, 100);
        else                       e.iconColorRGBA = packRgba(150, 200, 240);
        c.entries.push_back(e);
    };
    // Cataclysm layout: 3 Prime + 3 Major + 3 Minor.
    add(200, "CataPrime1", G::Prime, 0, 25, "Prime slot 1 - unlocks at 25.");
    add(201, "CataPrime2", G::Prime, 1, 50, "Prime slot 2 - unlocks at 50.");
    add(202, "CataPrime3", G::Prime, 2, 75, "Prime slot 3 - unlocks at 75.");
    add(203, "CataMajor1", G::Major, 0, 25, "Major slot 1 - unlocks at 25.");
    add(204, "CataMajor2", G::Major, 1, 50, "Major slot 2 - unlocks at 50.");
    add(205, "CataMajor3", G::Major, 2, 75, "Major slot 3 - unlocks at 75.");
    add(206, "CataMinor1", G::Minor, 0, 25, "Minor slot 1 - unlocks at 25.");
    add(207, "CataMinor2", G::Minor, 1, 50, "Minor slot 2 - unlocks at 50.");
    add(208, "CataMinor3", G::Minor, 2, 75, "Minor slot 3 - unlocks at 75.");
    return c;
}

} // namespace pipeline
} // namespace wowee
