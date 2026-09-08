#include "pipeline/wowee_tabards.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'B', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wtbd";

} // namespace

const WoweeTabards::Entry*
WoweeTabards::findById(uint32_t tabardId) const {
    for (const auto& e : entries)
        if (e.tabardId == tabardId) return &e;
    return nullptr;
}

std::vector<const WoweeTabards::Entry*>
WoweeTabards::findByGuild(uint32_t guildId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.guildId == guildId) out.push_back(&e);
    return out;
}

std::vector<const WoweeTabards::Entry*>
WoweeTabards::findApproved() const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.isApproved) out.push_back(&e);
    return out;
}

bool WoweeTabardsLoader::save(const WoweeTabards& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeTabards::Entry& e) {
        writePOD(os, e.tabardId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.backgroundPattern);
        writePOD(os, e.borderPattern);
        writePOD(os, e.emblemId);
        writePOD(os, e.backgroundColor);
        writePOD(os, e.borderColor);
        writePOD(os, e.emblemColor);
        writePOD(os, e.guildId);
        writePOD(os, e.creatorPlayerId);
        writePOD(os, e.isApproved);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeTabards WoweeTabardsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeTabards>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeTabards::Entry& e) {
        if (!readPOD(is, e.tabardId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.backgroundPattern) ||
            !readPOD(is, e.borderPattern) ||
            !readPOD(is, e.emblemId) ||
            !readPOD(is, e.backgroundColor) ||
            !readPOD(is, e.borderColor) ||
            !readPOD(is, e.emblemColor) ||
            !readPOD(is, e.guildId) ||
            !readPOD(is, e.creatorPlayerId) ||
            !readPOD(is, e.isApproved) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeTabardsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeTabards WoweeTabardsLoader::makeAllianceClassic(
    const std::string& catalogName) {
    using T = WoweeTabards;
    WoweeTabards c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t bgPat, uint32_t bgColor,
                    uint8_t bdPat, uint32_t bdColor,
                    uint16_t emblemId, uint32_t emblemColor,
                    const char* desc) {
        T::Entry e;
        e.tabardId = id; e.name = name; e.description = desc;
        e.backgroundPattern = bgPat;
        e.backgroundColor = bgColor;
        e.borderPattern = bdPat;
        e.borderColor = bdColor;
        e.emblemId = emblemId;
        e.emblemColor = emblemColor;
        e.guildId = 0;            // system tabard
        e.creatorPlayerId = 0;
        e.isApproved = 1;
        e.iconColorRGBA = packRgba(140, 200, 255);   // alliance blue
        c.entries.push_back(e);
    };
    add(1, "AllianceLion",
        T::Solid, packRgba(40, 80, 200),         // royal blue
        T::BorderDecorative, packRgba(220, 220, 100), // gold trim
        12, packRgba(220, 220, 100),
        "Royal Lion of Stormwind tabard - solid royal "
        "blue background, decorative gold trim, gold lion "
        "emblem. Granted by the Alliance championship "
        "questline.");
    add(2, "DwarvenHammer",
        T::Quartered, packRgba(180, 180, 180),    // silver
        T::BorderThick, packRgba(120, 80, 40),    // bronze
        17, packRgba(120, 80, 40),
        "Ironforge Hammer tabard - quartered silver "
        "background, thick bronze border, bronze hammer "
        "emblem. Awarded for Ironforge service.");
    add(3, "KulTirasAnchor",
        T::Gradient, packRgba(20, 40, 120),       // navy
        T::BorderThin, packRgba(180, 180, 180),   // silver
        24, packRgba(180, 180, 180),
        "Kul Tiran Naval Anchor tabard - navy-blue "
        "gradient, thin silver border, silver anchor "
        "emblem. Tides of Vengeance reward.");
    add(4, "HighlordSword",
        T::Starburst, packRgba(220, 200, 80),     // gold
        T::BorderDecorative, packRgba(255, 255, 255),
        31, packRgba(220, 60, 60),                // red sword
        "Highlord's Sword tabard - gold starburst "
        "background, ornate white border, crimson sword "
        "emblem. Argent Tournament Champion reward.");
    return c;
}

WoweeTabards WoweeTabardsLoader::makeHordeClassic(
    const std::string& catalogName) {
    using T = WoweeTabards;
    WoweeTabards c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t bgPat, uint32_t bgColor,
                    uint8_t bdPat, uint32_t bdColor,
                    uint16_t emblemId, uint32_t emblemColor,
                    const char* desc) {
        T::Entry e;
        e.tabardId = id; e.name = name; e.description = desc;
        e.backgroundPattern = bgPat;
        e.backgroundColor = bgColor;
        e.borderPattern = bdPat;
        e.borderColor = bdColor;
        e.emblemId = emblemId;
        e.emblemColor = emblemColor;
        e.guildId = 0;
        e.creatorPlayerId = 0;
        e.isApproved = 1;
        e.iconColorRGBA = packRgba(220, 80, 80);   // horde red
        c.entries.push_back(e);
    };
    add(100, "OrgrimmarWolfhead",
        T::Solid, packRgba(140, 30, 30),         // crimson
        T::BorderThick, packRgba(200, 200, 100),  // gold
        45, packRgba(200, 200, 100),
        "Wolf-head of Orgrimmar tabard - solid crimson "
        "background, thick gold border, gold wolfhead "
        "emblem. Awarded for Orgrimmar service.");
    add(101, "BarrensCrossedAxes",
        T::Chevron, packRgba(80, 30, 30),         // dark red
        T::BorderThin, packRgba(140, 100, 60),    // bronze
        51, packRgba(180, 180, 180),              // silver
        "Crossed Axes of the Barrens tabard - dark-red "
        "chevron background, thin bronze border, silver "
        "axes. Cross-faction battlemaster reward.");
    add(102, "ForsakenSkull",
        T::Solid, packRgba(20, 20, 20),           // black
        T::BorderDecorative, packRgba(140, 30, 30),
        58, packRgba(220, 220, 220),              // bone white
        "Forsaken Skull of Undercity tabard - solid "
        "black background, decorative crimson trim, bone-"
        "white skull emblem. Royal Apothecary reward.");
    add(103, "SilvermoonPyramid",
        T::Gradient, packRgba(200, 180, 100),     // tan
        T::BorderThin, packRgba(220, 60, 60),     // crimson
        66, packRgba(140, 30, 30),                // deep red
        "Silvermoon Pyramid tabard - tan gradient, "
        "thin crimson border, deep-red pyramid emblem. "
        "Sin'dorei lore quest reward.");
    return c;
}

WoweeTabards WoweeTabardsLoader::makeFactionVendor(
    const std::string& catalogName) {
    using T = WoweeTabards;
    WoweeTabards c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t bgPat, uint32_t bgColor,
                    uint8_t bdPat, uint32_t bdColor,
                    uint16_t emblemId, uint32_t emblemColor,
                    const char* desc) {
        T::Entry e;
        e.tabardId = id; e.name = name; e.description = desc;
        e.backgroundPattern = bgPat;
        e.backgroundColor = bgColor;
        e.borderPattern = bdPat;
        e.borderColor = bdColor;
        e.emblemId = emblemId;
        e.emblemColor = emblemColor;
        e.guildId = 0;
        e.creatorPlayerId = 0;
        e.isApproved = 1;
        e.iconColorRGBA = packRgba(180, 180, 240);   // faction lavender
        c.entries.push_back(e);
    };
    add(200, "ArgentCrusade",
        T::Solid, packRgba(220, 220, 220),
        T::BorderDecorative, packRgba(220, 220, 100),
        72, packRgba(220, 60, 60),
        "Argent Crusade tabard - silver background, "
        "ornate gold trim, crimson Light emblem. "
        "Honored standing required.");
    add(201, "EbonBlade",
        T::Solid, packRgba(20, 20, 20),
        T::BorderThick, packRgba(60, 80, 60),
        78, packRgba(180, 180, 180),
        "Knights of the Ebon Blade tabard - black "
        "background, thick green-iron border, silver "
        "rune. Honored standing required.");
    add(202, "SonsOfHodir",
        T::Gradient, packRgba(180, 200, 240),
        T::BorderThin, packRgba(140, 140, 200),
        85, packRgba(60, 80, 200),
        "Sons of Hodir tabard - frost-blue gradient, "
        "thin lavender border, deep-blue Hodir emblem. "
        "Honored standing required.");
    add(203, "WyrmrestAccord",
        T::Quartered, packRgba(180, 60, 60),
        T::BorderDecorative, packRgba(220, 220, 100),
        90, packRgba(220, 220, 220),
        "Wyrmrest Accord tabard - quartered crimson "
        "background, ornate gold trim, silver dragon "
        "emblem. Honored standing required.");
    add(204, "Kaluak",
        T::Chevron, packRgba(140, 200, 220),
        T::BorderThin, packRgba(80, 60, 40),
        96, packRgba(80, 60, 40),
        "The Kalu'ak tabard - sea-blue chevron, thin "
        "leather border, brown harpoon emblem. Revered "
        "standing required.");
    add(205, "FrenzyheartTribe",
        T::Starburst, packRgba(60, 120, 60),
        T::BorderThick, packRgba(180, 140, 80),
        102, packRgba(180, 140, 80),
        "Frenzyheart Tribe tabard - green starburst, "
        "tan leather border, ochre wolverine emblem. "
        "Conflicts with Oracles standing - pick one.");
    return c;
}

} // namespace pipeline
} // namespace wowee
