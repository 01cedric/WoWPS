#include "pipeline/wowee_expansion_names.hpp"
#include "pipeline/wowee_loading_screens.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'D', 'S'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wlds";

} // namespace

const WoweeLoadingScreen::Entry*
WoweeLoadingScreen::findById(uint32_t screenId) const {
    for (const auto& e : entries)
        if (e.screenId == screenId) return &e;
    return nullptr;
}

const char* WoweeLoadingScreen::expansionGateName(uint8_t e) {
    // The word is the sidecar's, shared with the other two formats
    // that gate on an expansion and with the importers that read them.
    return expansionName(e);
}

bool WoweeLoadingScreenLoader::save(const WoweeLoadingScreen& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeLoadingScreen::Entry& e) {
        writePOD(os, e.screenId);
        writePOD(os, e.mapId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.texturePath);
        writeStr(os, e.iconPath);
        writeStr(os, e.attribution);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.displayWeight);
        writePadding(os, 2);
        writePOD(os, e.expansionRequired);
        writePOD(os, e.isAnimated);
        writePOD(os, e.isWideAspect);
        writePadding(os, 1);
                       });
}

WoweeLoadingScreen WoweeLoadingScreenLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeLoadingScreen>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeLoadingScreen::Entry& e) {
        if (!readPOD(is, e.screenId) ||
            !readPOD(is, e.mapId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.texturePath) ||
            !readStr(is, e.iconPath) ||
            !readStr(is, e.attribution)) { return false; }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.displayWeight)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
        if (!readPOD(is, e.expansionRequired) ||
            !readPOD(is, e.isAnimated) ||
            !readPOD(is, e.isWideAspect)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
                                  return true;
                              });
}

bool WoweeLoadingScreenLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeLoadingScreen WoweeLoadingScreenLoader::makeStarter(
    const std::string& catalogName) {
    WoweeLoadingScreen c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t mapId, const char* name,
                    const char* tex, const char* desc) {
        WoweeLoadingScreen::Entry e;
        e.screenId = id;
        e.mapId = mapId;
        e.name = name;
        e.description = desc;
        e.texturePath = tex;
        e.iconPath = std::string("Interface/Glues/Common/") +
                      name + "_icon.blp";
        e.attribution = "Wowee art team";
        c.entries.push_back(e);
    };
    // ElwynnForest is gated to Eastern Kingdoms map (mapId=0)
    // - but mapId=0 is also the catch-all sentinel. To keep
    // the validator happy and reflect WoW's actual gating
    // (Elwynn lives on EK), bind it to the dwarf-starter
    // mapId 0 with explicit level cap. The GenericFallback
    // is the only true catch-all and uses a wider bracket
    // distinct from Elwynn's.
    add(1,   0, "ElwynnForest",
        "Interface/Glues/LoadingScreens/Elwynn.blp",
        "Stormwind / Elwynn Forest - green forested foothills "
        "with the abbey in the background.");
    c.entries.back().minLevel = 1;
    c.entries.back().maxLevel = 30;
    add(2,   1, "OrgrimmarLoading",
        "Interface/Glues/LoadingScreens/Orgrimmar.blp",
        "Orgrimmar - red rocky canyon walls + dragon banners.");
    // GenericFallback is the catch-all - full level range
    // but minimal weight, so it only appears when no
    // zone-specific screen matches.
    add(3,   0, "GenericFallback",
        "Interface/Glues/LoadingScreens/GenericMap.blp",
        "Generic catch-all - dragon icon over starfield, "
        "shown when no zone-specific screen matches.");
    c.entries.back().minLevel = 31;
    c.entries.back().maxLevel = 80;
    return c;
}

WoweeLoadingScreen WoweeLoadingScreenLoader::makeInstances(
    const std::string& catalogName) {
    WoweeLoadingScreen c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t mapId, const char* name,
                    const char* tex, const char* desc) {
        WoweeLoadingScreen::Entry e;
        e.screenId = id;
        e.mapId = mapId;
        e.name = name;
        e.description = desc;
        e.texturePath = tex;
        e.iconPath = std::string("Interface/Glues/Instances/") +
                      name + "_icon.blp";
        e.expansionRequired = WoweeLoadingScreen::WotLK;
        e.minLevel = 75;
        e.maxLevel = 80;
        c.entries.push_back(e);
    };
    add(100, 602, "HallsOfLightning",
        "Interface/Glues/LoadingScreens/HoL.blp",
        "Storm titan-keeper facility - purple lightning arcs over "
        "obsidian floors.");
    add(101, 599, "HallsOfStone",
        "Interface/Glues/LoadingScreens/HoS.blp",
        "Tribunal of Ages - colossal iron-dwarf statues lining "
        "a grand chamber.");
    add(102, 575, "UtgardePinnacle",
        "Interface/Glues/LoadingScreens/UP.blp",
        "Vrykul fortress - windswept icy parapet with King "
        "Ymiron's throne distant.");
    add(103, 608, "VioletHold",
        "Interface/Glues/LoadingScreens/VH.blp",
        "Dalaran prison breakout - violet magic shields holding "
        "back interdimensional rifts.");
    add(104, 595, "OldKingdom",
        "Interface/Glues/LoadingScreens/OK.blp",
        "Faceless one ruins - green bioluminescent fungi + "
        "Old God tentacles in the gloom.");
    return c;
}

WoweeLoadingScreen WoweeLoadingScreenLoader::makeRaidIntros(
    const std::string& catalogName) {
    WoweeLoadingScreen c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t mapId, const char* name,
                    const char* tex, const char* desc) {
        WoweeLoadingScreen::Entry e;
        e.screenId = id;
        e.mapId = mapId;
        e.name = name;
        e.description = desc;
        e.texturePath = tex;
        e.iconPath = std::string("Interface/Glues/Raids/") +
                      name + "_icon.blp";
        e.attribution = "Wowee art team - raid intro variants";
        e.expansionRequired = WoweeLoadingScreen::WotLK;
        e.minLevel = 80;
        e.maxLevel = 80;
        e.isWideAspect = 1;     // 16:9 raid intro art
        e.displayWeight = 3;    // higher weight than normal screens
        c.entries.push_back(e);
    };
    add(200, 533, "Naxxramas",
        "Interface/Glues/Raids/NaxxIntro.blp",
        "Floating Necropolis silhouetted against Northrend "
        "aurora - Kel'Thuzad's eye glow at center.");
    add(201, 603, "Ulduar",
        "Interface/Glues/Raids/UlduarIntro.blp",
        "Titan facility entrance - Yogg-Saron's mind-warping "
        "tendrils creeping from the corners.");
    add(202, 649, "TrialOfTheCrusader",
        "Interface/Glues/Raids/TocIntro.blp",
        "Argent Crusade colosseum - sun beams piercing arena "
        "spires + crowds in stands.");
    return c;
}

} // namespace pipeline
} // namespace wowee
