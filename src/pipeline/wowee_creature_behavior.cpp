#include "pipeline/wowee_creature_behavior.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'H', 'V'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wbhv";

} // namespace

const WoweeCreatureBehavior::Entry*
WoweeCreatureBehavior::findById(uint32_t behaviorId) const {
    for (const auto& e : entries)
        if (e.behaviorId == behaviorId) return &e;
    return nullptr;
}

std::vector<const WoweeCreatureBehavior::Entry*>
WoweeCreatureBehavior::findByKind(uint8_t creatureKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.creatureKind == creatureKind) out.push_back(&e);
    return out;
}

bool WoweeCreatureBehaviorLoader::save(
    const WoweeCreatureBehavior& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.behaviorId);
        writeStr(os, e.name);
        writePOD(os, e.creatureKind);
        writePOD(os, e.evadeBehavior);
        writePOD(os, e.pad0);
        writePOD(os, e.aggroRadius);
        writePOD(os, e.leashRadius);
        writePOD(os, e.corpseDurationSec);
        writePOD(os, e.mainAttackSpellId);
        uint32_t specCount =
            static_cast<uint32_t>(e.specialAbilities.size());
        writePOD(os, specCount);
        for (const auto& s : e.specialAbilities) {
            writePOD(os, s.spellId);
            writePOD(os, s.cooldownMs);
            writePOD(os, s.useChancePct);
            writePOD(os, s.pad1);
        }
    });
}

WoweeCreatureBehavior WoweeCreatureBehaviorLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCreatureBehavior>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCreatureBehavior::Entry& e) {
        if (!readPOD(is, e.behaviorId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.creatureKind) ||
            !readPOD(is, e.evadeBehavior) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.aggroRadius) ||
            !readPOD(is, e.leashRadius) ||
            !readPOD(is, e.corpseDurationSec) ||
            !readPOD(is, e.mainAttackSpellId)) { return false; }
        uint32_t specCount = 0;
        if (!readPOD(is, specCount)) { return false; }
        // Sanity cap - real bosses cap at ~6
        // abilities; format cap 32.
        if (specCount > 32) { return false; }
        e.specialAbilities.resize(specCount);
        for (auto& s : e.specialAbilities) {
            if (!readPOD(is, s.spellId) ||
                !readPOD(is, s.cooldownMs) ||
                !readPOD(is, s.useChancePct) ||
                !readPOD(is, s.pad1)) { return false; }
        }
                                  return true;
                              });
}

bool WoweeCreatureBehaviorLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeCreatureBehavior::Entry makeBehavior(
    uint32_t behaviorId, const char* name,
    uint8_t creatureKind, uint8_t evadeBehavior,
    float aggroRadius, float leashRadius,
    uint32_t corpseDurationSec, uint32_t mainAttackSpellId,
    std::vector<WoweeCreatureBehavior::SpecialAbility>
        specials) {
    WoweeCreatureBehavior::Entry e;
    e.behaviorId = behaviorId; e.name = name;
    e.creatureKind = creatureKind;
    e.evadeBehavior = evadeBehavior;
    e.aggroRadius = aggroRadius;
    e.leashRadius = leashRadius;
    e.corpseDurationSec = corpseDurationSec;
    e.mainAttackSpellId = mainAttackSpellId;
    e.specialAbilities = std::move(specials);
    return e;
}

WoweeCreatureBehavior::SpecialAbility makeSpec(
    uint32_t spellId, uint32_t cooldownMs,
    uint16_t useChancePct) {
    WoweeCreatureBehavior::SpecialAbility s;
    s.spellId = spellId;
    s.cooldownMs = cooldownMs;
    s.useChancePct = useChancePct;
    return s;
}

} // namespace

WoweeCreatureBehavior WoweeCreatureBehaviorLoader::makeMeleeBehaviors(
    const std::string& catalogName) {
    using B = WoweeCreatureBehavior;
    WoweeCreatureBehavior c;
    c.name = catalogName;
    // Kobold: aggro 8yd, leash 30yd, melee swing
    // (spellId 0 = no override = use default melee).
    // 1 special: throw-rock at 5% chance.
    c.entries.push_back(makeBehavior(
        1, "Kobold Worker",
        B::Melee, B::ResetToSpawn,
        8.f, 30.f, 60, 0,
        {makeSpec(11876 /* throw rock */, 8000, 500)}));
    // Wolf: aggro 10yd (predator scent), pet-style
    // claw-bite rotation.
    c.entries.push_back(makeBehavior(
        2, "Timber Wolf",
        B::Beast, B::ResetToSpawn,
        10.f, 35.f, 60, 0,
        {makeSpec(3009 /* claw */, 5000, 1500)}));
    // Raptor: aggro 9yd, faster ResetToSpawn (these
    // are noted runners). 1 chase-leap special.
    c.entries.push_back(makeBehavior(
        3, "Stranglethorn Raptor",
        B::Beast, B::ResetToSpawn,
        9.f, 40.f, 60, 0,
        {makeSpec(7165 /* leap */, 12000, 2000)}));
    return c;
}

WoweeCreatureBehavior
WoweeCreatureBehaviorLoader::makeCasterBehaviors(
    const std::string& catalogName) {
    using B = WoweeCreatureBehavior;
    WoweeCreatureBehavior c;
    c.name = catalogName;
    // Defias Wizard: caster, fireball default rotation,
    // Polymorph + Frost Nova specials.
    c.entries.push_back(makeBehavior(
        10, "Defias Wizard",
        B::Caster, B::ResetToSpawn,
        20.f, 60.f, 60, 133 /* Fireball */,
        {makeSpec(118 /* Polymorph */, 30000, 3000),
         makeSpec(122 /* Frost Nova */, 25000, 2500)}));
    // Murloc Coastrunner: low-level caster with bolt-
    // type ability + heal-self special.
    c.entries.push_back(makeBehavior(
        11, "Murloc Coastrunner",
        B::Caster, B::ResetToSpawn,
        15.f, 35.f, 60, 11979 /* Frost Bolt */,
        {makeSpec(2050 /* Lesser Heal */, 20000, 1500)}));
    // Voidwalker (warlock pet pattern): tank-caster
    // hybrid with taunt + sacrifice.
    c.entries.push_back(makeBehavior(
        12, "Voidwalker Pet Pattern",
        B::Tank, B::ResetToSpawn,
        12.f, 40.f, 60, 0 /* default melee */,
        {makeSpec(7264 /* Taunt */, 10000, 5000),
         makeSpec(7812 /* Sacrifice */, 0, 0),  // 0%
                                                 //  use,
                                                 //  master-
                                                 //  triggered
                                                 //  only
         makeSpec(17767 /* Suffering */, 30000, 2000)}));
    return c;
}

WoweeCreatureBehavior WoweeCreatureBehaviorLoader::makeBossBehaviors(
    const std::string& catalogName) {
    using B = WoweeCreatureBehavior;
    WoweeCreatureBehavior c;
    c.name = catalogName;
    // Onyxia-pattern boss: NoEvade (raid bosses
    // permanent-leash to encounter zone), 600s
    // (10min) corpse duration so 40-man raid can
    // distribute loot. 4 abilities in rotation.
    c.entries.push_back(makeBehavior(
        100, "Onyxia-Pattern Dragon Boss",
        B::Tank, B::NoEvade,   // dragons render as
                                //  Tank kind for AI
                                //  threat dispatch
        50.f, 999.f, 600, 0 /* default melee + flame */,
        {makeSpec(18395 /* Wing Buffet */, 25000, 4000),
         makeSpec(18392 /* Flame Breath */, 12000, 5000),
         makeSpec(18435 /* Tail Sweep */, 20000, 3500),
         makeSpec(18650 /* Deep Breath */, 90000, 2500)}));
    return c;
}

} // namespace pipeline
} // namespace wowee
