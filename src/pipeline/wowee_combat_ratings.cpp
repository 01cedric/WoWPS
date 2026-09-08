#include "pipeline/wowee_combat_ratings.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'R', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcrr";

} // namespace

const WoweeCombatRating::Entry*
WoweeCombatRating::findById(uint32_t ratingType) const {
    for (const auto& e : entries)
        if (e.ratingType == ratingType) return &e;
    return nullptr;
}

const char* WoweeCombatRating::ratingKindName(uint8_t k) {
    switch (k) {
        case Combat:     return "combat";
        case Defense:    return "defense";
        case Spell:      return "spell";
        case Resilience: return "resilience";
        case Other:      return "other";
        default:         return "unknown";
    }
}

bool WoweeCombatRatingLoader::save(const WoweeCombatRating& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCombatRating::Entry& e) {
        writePOD(os, e.ratingType);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.ratingKind);
        writePadding(os, 3);
        writePOD(os, e.pointsAtL1);
        writePOD(os, e.pointsAtL60);
        writePOD(os, e.pointsAtL70);
        writePOD(os, e.pointsAtL80);
        writePOD(os, e.maxBenefitPercent);
                       });
}

WoweeCombatRating WoweeCombatRatingLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCombatRating>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCombatRating::Entry& e) {
        if (!readPOD(is, e.ratingType)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.ratingKind)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.pointsAtL1) ||
            !readPOD(is, e.pointsAtL60) ||
            !readPOD(is, e.pointsAtL70) ||
            !readPOD(is, e.pointsAtL80) ||
            !readPOD(is, e.maxBenefitPercent)) { return false; }
                                  return true;
                              });
}

bool WoweeCombatRatingLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeCombatRating WoweeCombatRatingLoader::makeStarter(
    const std::string& catalogName) {
    WoweeCombatRating c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float l1, float l60,
                    float l70, float l80, float maxPct,
                    const char* desc) {
        WoweeCombatRating::Entry e;
        e.ratingType = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_") +
                      name + ".blp";
        e.ratingKind = WoweeCombatRating::Combat;
        e.pointsAtL1 = l1;
        e.pointsAtL60 = l60;
        e.pointsAtL70 = l70;
        e.pointsAtL80 = l80;
        e.maxBenefitPercent = maxPct;
        c.entries.push_back(e);
    };
    // Canonical WoW WotLK conversion values.
    add(1, "HitRating",   1.0f, 10.0f, 15.7f, 32.79f, 100.0f,
        "Increases chance to hit. ~32.79 hit rating = 1% hit "
        "at level 80.");
    add(2, "CritRating",  1.0f, 14.0f, 22.1f, 45.91f, 100.0f,
        "Increases critical strike chance. ~45.91 crit rating "
        "= 1% crit at level 80.");
    add(3, "HasteRating", 1.0f, 10.0f, 15.8f, 32.79f, 100.0f,
        "Increases attack and casting speed. ~32.79 haste "
        "rating = 1% haste at level 80.");
    return c;
}

WoweeCombatRating WoweeCombatRatingLoader::makeDefensive(
    const std::string& catalogName) {
    WoweeCombatRating c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float l1, float l60,
                    float l70, float l80, float maxPct,
                    const char* desc) {
        WoweeCombatRating::Entry e;
        e.ratingType = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_Defense_") +
                      name + ".blp";
        e.ratingKind = WoweeCombatRating::Defense;
        e.pointsAtL1 = l1;
        e.pointsAtL60 = l60;
        e.pointsAtL70 = l70;
        e.pointsAtL80 = l80;
        e.maxBenefitPercent = maxPct;
        c.entries.push_back(e);
    };
    add(100, "DefenseRating", 1.0f,  1.5f,  2.4f,   4.92f, 100.0f,
        "Increases defense skill. ~4.92 = 1 defense at L80; "
        "1 defense reduces crit chance against you by 0.04%.");
    add(101, "DodgeRating",   1.0f, 12.0f, 18.9f,  39.34f,  60.0f,
        "Increases dodge chance. Diminishing returns soft cap "
        "around 30%.");
    add(102, "ParryRating",   1.0f, 13.0f, 20.5f,  43.84f,  50.0f,
        "Increases parry chance. Tank-only stat.");
    add(103, "BlockRating",   1.0f,  5.0f,  7.9f,  16.39f,  60.0f,
        "Increases shield block chance.");
    return c;
}

WoweeCombatRating WoweeCombatRatingLoader::makeSpell(
    const std::string& catalogName) {
    WoweeCombatRating c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float l1, float l60,
                    float l70, float l80, float maxPct,
                    uint8_t kind, const char* desc) {
        WoweeCombatRating::Entry e;
        e.ratingType = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_Holy_") +
                      name + ".blp";
        e.ratingKind = kind;
        e.pointsAtL1 = l1;
        e.pointsAtL60 = l60;
        e.pointsAtL70 = l70;
        e.pointsAtL80 = l80;
        e.maxBenefitPercent = maxPct;
        c.entries.push_back(e);
    };
    add(200, "SpellPower",        1.0f, 1.0f, 1.0f, 1.0f, 100.0f,
        WoweeCombatRating::Spell,
        "Direct 1:1 conversion - 1 spell power adds 1 to "
        "spell damage / heal scaling pre-coefficient.");
    add(201, "SpellPenetration",  1.0f, 1.0f, 1.0f, 1.0f, 130.0f,
        WoweeCombatRating::Spell,
        "Reduces target's spell resistance by 1 per point "
        "(no level scaling - flat).");
    add(202, "ManaPer5Seconds",   1.0f, 1.0f, 1.0f, 1.0f, 9999.0f,
        WoweeCombatRating::Spell,
        "Mana regenerated every 5 seconds (combat + out-of-"
        "combat). Direct value, no conversion.");
    return c;
}

} // namespace pipeline
} // namespace wowee
