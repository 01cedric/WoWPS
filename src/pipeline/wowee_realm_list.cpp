#include "pipeline/wowee_realm_list.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'S', 'P'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wmsp";

} // namespace

const WoweeRealmList::Entry*
WoweeRealmList::findById(uint32_t realmId) const {
    for (const auto& e : entries)
        if (e.realmId == realmId) return &e;
    return nullptr;
}

std::vector<const WoweeRealmList::Entry*>
WoweeRealmList::findByExpansion(uint8_t maxExpansion) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.expansion <= maxExpansion) out.push_back(&e);
    return out;
}

std::vector<const WoweeRealmList::Entry*>
WoweeRealmList::findByType(uint8_t realmType) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.realmType == realmType) out.push_back(&e);
    return out;
}

bool WoweeRealmListLoader::save(const WoweeRealmList& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeRealmList::Entry& e) {
        writePOD(os, e.realmId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.address);
        writePOD(os, e.realmType);
        writePOD(os, e.realmCategory);
        writePOD(os, e.expansion);
        writePOD(os, e.population);
        writePOD(os, e.characterCap);
        writePOD(os, e.gmOnly);
        writePOD(os, e.timezone);
        writePOD(os, e.pad0);
        writePOD(os, e.versionMajor);
        writePOD(os, e.versionMinor);
        writePOD(os, e.versionPatch);
        writePOD(os, e.pad1);
        writePOD(os, e.buildNumber);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeRealmList WoweeRealmListLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeRealmList>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeRealmList::Entry& e) {
        if (!readPOD(is, e.realmId)) { return false; }
        if (!readStr(is, e.name) ||
            !readStr(is, e.description) ||
            !readStr(is, e.address)) { return false; }
        if (!readPOD(is, e.realmType) ||
            !readPOD(is, e.realmCategory) ||
            !readPOD(is, e.expansion) ||
            !readPOD(is, e.population) ||
            !readPOD(is, e.characterCap) ||
            !readPOD(is, e.gmOnly) ||
            !readPOD(is, e.timezone) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.versionMajor) ||
            !readPOD(is, e.versionMinor) ||
            !readPOD(is, e.versionPatch) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.buildNumber) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeRealmListLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeRealmList WoweeRealmListLoader::makeSingleRealm(
    const std::string& catalogName) {
    using R = WoweeRealmList;
    WoweeRealmList c;
    c.name = catalogName;
    R::Entry e;
    e.realmId = 1;
    e.name = "WoweeMain";
    e.description =
        "Default Wowee server realm - WotLK 3.3.5a, Normal "
        "PvE rule-set, public category, US East timezone, "
        "10-character cap per account.";
    e.address = "logon.wowee.example.com:8085";
    e.realmType = R::Normal;
    e.realmCategory = R::Public;
    e.expansion = R::WotLK;
    e.population = R::Medium;
    e.characterCap = 10;
    e.gmOnly = 0;
    e.timezone = 8;
    e.versionMajor = 3; e.versionMinor = 3; e.versionPatch = 5;
    e.buildNumber = 12340;
    e.iconColorRGBA = packRgba(140, 200, 255);    // realm blue
    c.entries.push_back(e);
    return c;
}

WoweeRealmList WoweeRealmListLoader::makePvPCluster(
    const std::string& catalogName) {
    using R = WoweeRealmList;
    WoweeRealmList c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    uint8_t pop, uint32_t color, const char* desc) {
        R::Entry e;
        e.realmId = id; e.name = name; e.description = desc;
        e.address = "cluster.wowee.example.com:8085";
        e.realmType = type;
        e.realmCategory = R::Public;
        e.expansion = R::WotLK;
        e.population = pop;
        e.characterCap = 10;
        e.gmOnly = 0;
        e.timezone = 8;
        e.versionMajor = 3; e.versionMinor = 3; e.versionPatch = 5;
        e.buildNumber = 12340;
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    add(1, "WoweePvE", R::Normal, R::Medium,
        packRgba(140, 200, 255),
        "Cluster realm - Normal PvE rule-set. World PvP "
        "is opt-in; ganking flagged as harassment.");
    add(2, "WoweePvP", R::PvP, R::High,
        packRgba(220, 80, 100),
        "Cluster realm - PvP rule-set. Cross-faction "
        "world combat is always-on outside of cities.");
    add(3, "WoweeRP", R::RP, R::Low,
        packRgba(180, 100, 240),
        "Cluster realm - Roleplay. Naming policy "
        "enforced; in-character chat encouraged.");
    return c;
}

WoweeRealmList WoweeRealmListLoader::makeMultiExpansion(
    const std::string& catalogName) {
    using R = WoweeRealmList;
    WoweeRealmList c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t expansion,
                    uint8_t verMajor, uint8_t verMinor,
                    uint8_t verPatch, uint32_t build,
                    uint32_t color, const char* desc) {
        R::Entry e;
        e.realmId = id; e.name = name; e.description = desc;
        e.address = "logon.wowee.example.com:8085";
        e.realmType = R::Normal;
        e.realmCategory = R::Public;
        e.expansion = expansion;
        e.population = R::Medium;
        e.characterCap = 10;
        e.gmOnly = 0;
        e.timezone = 8;
        e.versionMajor = verMajor;
        e.versionMinor = verMinor;
        e.versionPatch = verPatch;
        e.buildNumber = build;
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    add(1, "Wowee-Vanilla", R::Vanilla, 1, 12, 1, 5875,
        packRgba(220, 220, 100),
        "Vanilla 1.12.1 progression realm - original "
        "60-cap content, no Outland zones.");
    add(2, "Wowee-TBC", R::TBC, 2, 4, 3, 8606,
        packRgba(100, 220, 100),
        "TBC 2.4.3 progression realm - Outland + 70-cap "
        "content, Sunwell endgame.");
    add(3, "Wowee-WotLK", R::WotLK, 3, 3, 5, 12340,
        packRgba(140, 200, 255),
        "WotLK 3.3.5a progression realm - Northrend + "
        "80-cap content, ICC endgame.");
    add(4, "Wowee-Cata", R::Cata, 4, 3, 4, 15595,
        packRgba(220, 130, 80),
        "Cata 4.3.4 progression realm - post-Shattering "
        "world + 85-cap content, DS endgame. Currently "
        "Beta access only.");
    // Mark Cata as beta category (override the default
    // Public set by the lambda).
    if (!c.entries.empty()) {
        c.entries.back().realmCategory = R::Beta;
        c.entries.back().population = R::Low;
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
