#include "pipeline/wowee_addon_manifest.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'O', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wmod";



} // namespace

const WoweeAddonManifest::Entry*
WoweeAddonManifest::findById(uint32_t addonId) const {
    for (const auto& e : entries)
        if (e.addonId == addonId) return &e;
    return nullptr;
}

const WoweeAddonManifest::Entry*
WoweeAddonManifest::findByName(const std::string& nm) const {
    for (const auto& e : entries)
        if (e.name == nm) return &e;
    return nullptr;
}

std::vector<const WoweeAddonManifest::Entry*>
WoweeAddonManifest::findDependents(uint32_t addonId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        for (uint32_t d : e.dependencies) {
            if (d == addonId) { out.push_back(&e); break; }
        }
    }
    return out;
}

bool WoweeAddonManifestLoader::save(
    const WoweeAddonManifest& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.addonId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.version);
        writeStr(os, e.author);
        writePOD(os, e.minClientBuild);
        writePOD(os, e.requiresSavedVariables);
        writePOD(os, e.loadOnDemand);
        writePOD(os, e.pad0);
        writeU32Vec(os, e.dependencies);
        writeU32Vec(os, e.optionalDependencies);
    });
}

WoweeAddonManifest WoweeAddonManifestLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeAddonManifest>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeAddonManifest::Entry& e) {
        if (!readPOD(is, e.addonId)) { return false; }
        if (!readStr(is, e.name) ||
            !readStr(is, e.description) ||
            !readStr(is, e.version) ||
            !readStr(is, e.author)) { return false; }
        if (!readPOD(is, e.minClientBuild) ||
            !readPOD(is, e.requiresSavedVariables) ||
            !readPOD(is, e.loadOnDemand) ||
            !readPOD(is, e.pad0)) { return false; }
        if (!readU32Vec(is, e.dependencies) ||
            !readU32Vec(is, e.optionalDependencies)) { return false; }
                                  return true;
                              });
}

bool WoweeAddonManifestLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeAddonManifest WoweeAddonManifestLoader::makeStandardAddons(
    const std::string& catalogName) {
    using A = WoweeAddonManifest;
    WoweeAddonManifest c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* version, const char* author,
                    uint8_t needsSV, uint8_t lod,
                    std::vector<uint32_t> deps,
                    std::vector<uint32_t> optDeps,
                    const char* desc) {
        A::Entry e;
        e.addonId = id; e.name = name;
        e.version = version; e.author = author;
        e.description = desc;
        e.minClientBuild = 5875;            // 1.12 vanilla
                                              // build floor
        e.requiresSavedVariables = needsSV;
        e.loadOnDemand = lod;
        e.dependencies = std::move(deps);
        e.optionalDependencies = std::move(optDeps);
        c.entries.push_back(e);
    };
    // Recount: standalone DPS meter - no deps,
    // persists session combat history.
    add(1, "Recount", "2.0.4", "Cryect/Elsia", 1, 0,
        {}, {},
        "Damage meter - tracks DPS/HPS/threat per "
        "encounter. Saves recent combat sessions to "
        "SavedVariables.");
    // Atlas: standalone instance map browser, no deps,
    // no persistence.
    add(2, "Atlas", "1.10.2", "DanGilbert", 0, 0,
        {}, {},
        "Instance map browser - shows boss + loot "
        "locations for vanilla dungeons / raids. "
        "Static data, no SavedVariables.");
    // Auctioneer: optionally depends on Atlas for
    // map-link buttons in scan history (graceful
    // degradation if Atlas absent).
    add(3, "Auctioneer", "5.21.5497", "Norganna", 1, 0,
        {}, {2},
        "Auction house scanner + market analysis. "
        "Optionally uses Atlas for map links in scan "
        "history (degrades gracefully if absent).");
    // Questie: standalone quest helper, persists quest
    // log + completed-quest cache.
    add(4, "Questie", "4.4.1", "Questie-Team", 1, 0,
        {}, {},
        "Quest helper - overlay markers + objective "
        "tracking. Persists per-character completed "
        "quest list to SavedVariables.");
    return c;
}

WoweeAddonManifest WoweeAddonManifestLoader::makeUIReplacement(
    const std::string& catalogName) {
    using A = WoweeAddonManifest;
    WoweeAddonManifest c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* version, const char* author,
                    std::vector<uint32_t> deps,
                    const char* desc) {
        A::Entry e;
        e.addonId = id; e.name = name;
        e.version = version; e.author = author;
        e.description = desc;
        e.minClientBuild = 5875;
        e.requiresSavedVariables = 1;       // UI mods
                                              // always need
                                              // settings
                                              // persistence
        e.loadOnDemand = 0;
        e.dependencies = std::move(deps);
        c.entries.push_back(e);
    };
    // Bartender4: action-bar replacement, root of the
    // UI-replacement dep chain.
    add(10, "Bartender4", "4.5.5", "Nevcairiel",
        {},
        "Action-bar replacement - supports 10 movable "
        "bars with per-bar visibility states. Standalone "
        "root of the UI-replacement dep chain.");
    // ElvUI: full UI replacement - depends on
    // Bartender4 for action-bar layer (real ElvUI
    // ships its own bar mod, but for this preset we
    // model the dep chain).
    add(11, "ElvUI", "1.21", "TukUI-Team",
        {10},
        "Full UI replacement - unitframes / nameplates "
        "/ chat / minimap. Depends on Bartender4 for "
        "the action-bar layer (preset models a chain).");
    // SuperOrders: ElvUI extension for raid frames -
    // requires ElvUI.
    add(12, "SuperOrders", "0.9.3", "RaidLeader",
        {11},
        "ElvUI raid-frame extension - adds clickcast "
        "+ smartheal. Requires ElvUI as parent.");
    return c;
}

WoweeAddonManifest WoweeAddonManifestLoader::makeUtility(
    const std::string& catalogName) {
    using A = WoweeAddonManifest;
    WoweeAddonManifest c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* version, const char* author,
                    uint8_t lod,
                    const char* desc) {
        A::Entry e;
        e.addonId = id; e.name = name;
        e.version = version; e.author = author;
        e.description = desc;
        e.minClientBuild = 5875;
        e.requiresSavedVariables = 0;
        e.loadOnDemand = lod;
        c.entries.push_back(e);
    };
    add(20, "XPerl", "3.7.5", "ZenTabi/XPerl-Team", 0,
        "Unit-frame replacement - drop-in UI mod, no "
        "deps, no persistence. Default-load.");
    add(21, "Decursive", "2.7.7", "Archarodim", 0,
        "Auto-decurse mouseover - keybind helper for "
        "removing harmful auras. Default-load.");
    // GearVendor is loadOnDemand: only loads when the
    // user opens the gear-comparison popup.
    add(22, "GearVendor", "1.0.2", "GearLab", 1,
        "Item upgrade comparison popup - loadOnDemand: "
        "skipped at login, loaded only when popup "
        "opens. Saves favorite-item list.");
    return c;
}

} // namespace pipeline
} // namespace wowee
