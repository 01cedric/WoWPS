#include "pipeline/wowee_spell_proc_rules.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'R', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wprc";

} // namespace

const WoweeSpellProcRules::Entry*
WoweeSpellProcRules::findById(uint32_t procRuleId) const {
    for (const auto& e : entries)
        if (e.procRuleId == procRuleId) return &e;
    return nullptr;
}

std::vector<const WoweeSpellProcRules::Entry*>
WoweeSpellProcRules::findBySourceSpell(uint32_t spellId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.sourceSpellId == spellId) out.push_back(&e);
    return out;
}

std::vector<const WoweeSpellProcRules::Entry*>
WoweeSpellProcRules::findByEvent(uint8_t triggerEvent) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.triggerEvent == triggerEvent) out.push_back(&e);
    return out;
}

bool WoweeSpellProcRulesLoader::save(
    const WoweeSpellProcRules& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.procRuleId);
        writeStr(os, e.name);
        writePOD(os, e.sourceSpellId);
        writePOD(os, e.procEffectSpellId);
        writePOD(os, e.triggerEvent);
        writePOD(os, e.maxStacksOnTarget);
        writePOD(os, e.procChancePct);
        writePOD(os, e.internalCooldownMs);
        writePOD(os, e.procFlagsMask);
        writePOD(os, e.pad0);
    });
}

WoweeSpellProcRules WoweeSpellProcRulesLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeSpellProcRules>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeSpellProcRules::Entry& e) {
        if (!readPOD(is, e.procRuleId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.sourceSpellId) ||
            !readPOD(is, e.procEffectSpellId) ||
            !readPOD(is, e.triggerEvent) ||
            !readPOD(is, e.maxStacksOnTarget) ||
            !readPOD(is, e.procChancePct) ||
            !readPOD(is, e.internalCooldownMs) ||
            !readPOD(is, e.procFlagsMask) ||
            !readPOD(is, e.pad0)) { return false; }
                                  return true;
                              });
}

bool WoweeSpellProcRulesLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeSpellProcRules::Entry makeRule(
    uint32_t procRuleId, const char* name,
    uint32_t sourceSpellId, uint32_t procEffectSpellId,
    uint8_t triggerEvent,
    uint16_t procChancePct,
    uint32_t internalCooldownMs,
    uint16_t procFlagsMask,
    uint8_t maxStacksOnTarget = 0) {
    WoweeSpellProcRules::Entry e;
    e.procRuleId = procRuleId; e.name = name;
    e.sourceSpellId = sourceSpellId;
    e.procEffectSpellId = procEffectSpellId;
    e.triggerEvent = triggerEvent;
    e.procChancePct = procChancePct;
    e.internalCooldownMs = internalCooldownMs;
    e.procFlagsMask = procFlagsMask;
    e.maxStacksOnTarget = maxStacksOnTarget;
    return e;
}

} // namespace

WoweeSpellProcRules WoweeSpellProcRulesLoader::makeWeaponProcs(
    const std::string& catalogName) {
    using P = WoweeSpellProcRules;
    WoweeSpellProcRules c;
    c.name = catalogName;
    // Crusader (Enchant Weapon - Crusader, spellId
    // 20007) procs spell 20007 buff OnHit at ~2%
    // chance, no ICD, requires melee weapon.
    c.entries.push_back(makeRule(
        1, "Crusader Weapon Enchant",
        20007 /* Enchant: Crusader */,
        20007 /* same spell triggers the buff */,
        P::OnHit,
        200 /* 2% basis points */,
        0,
        P::RequireMeleeWeapon | P::ExcludeAutoAttack));
    // Lifesteal: spellId 20004 (Enchant Weapon -
    // Lifestealing) procs heal-on-hit. 5% chance,
    // no ICD.
    c.entries.push_back(makeRule(
        2, "Lifestealing Weapon Enchant",
        20004 /* Enchant: Lifesteal */,
        20004,
        P::OnHit,
        500 /* 5% */,
        0,
        P::RequireMeleeWeapon));
    // Fiery Weapon (spellId 13898): 7% chance fire
    // damage proc, 1.5s ICD to prevent
    // double-procs on dual-wield.
    c.entries.push_back(makeRule(
        3, "Fiery Weapon Enchant",
        13898 /* Enchant: Fiery Weapon */,
        13898,
        P::OnHit,
        700 /* 7% */,
        1500 /* 1.5s ICD */,
        P::RequireMeleeWeapon));
    return c;
}

WoweeSpellProcRules WoweeSpellProcRulesLoader::makeRetPaladin(
    const std::string& catalogName) {
    using P = WoweeSpellProcRules;
    WoweeSpellProcRules c;
    c.name = catalogName;
    // Vengeance: each crit grants 5-stack Vengeance
    // buff (spellId 9452 procs effect 9452 OnCrit).
    // 100% chance, no ICD, max 5 stacks.
    c.entries.push_back(makeRule(
        10, "Vengeance Paladin Crit Stack",
        9452 /* Vengeance talent */,
        9452 /* stacking buff */,
        P::OnCrit,
        10000 /* 100% - every crit */,
        0,
        0,
        5 /* max 5 stacks */));
    // Seal of Justice: 25% chance to stun on hit
    // (spellId 20164 sourceAura procs spell 20170
    // stun effect). 60s ICD per target to prevent
    // perma-stun.
    c.entries.push_back(makeRule(
        11, "Seal of Justice Stun",
        20164 /* Seal of Justice aura */,
        20170 /* Justice stun effect */,
        P::OnHit,
        2500 /* 25% */,
        60000 /* 1min ICD per target */,
        P::RequireMeleeWeapon));
    // Reckoning: 10% chance on block to gain extra
    // attack. Ret talent.
    c.entries.push_back(makeRule(
        12, "Reckoning Block-to-Attack",
        20176 /* Reckoning talent */,
        20178 /* extra-attack effect */,
        P::OnBlock,
        1000 /* 10% */,
        0,
        0));
    // Sanctity Aura: 100% on cast, amplifies holy
    // damage by 10%. Aura is permanent so trigger is
    // just OnCast bookkeeping.
    c.entries.push_back(makeRule(
        13, "Sanctity Aura Holy Amp",
        20218 /* Sanctity Aura */,
        20221 /* Holy-amp passive */,
        P::OnCast,
        10000 /* 100% - bookkeeping */,
        0,
        P::RequireSpellSchool));
    return c;
}

WoweeSpellProcRules WoweeSpellProcRulesLoader::makeRageGen(
    const std::string& catalogName) {
    using P = WoweeSpellProcRules;
    WoweeSpellProcRules c;
    c.name = catalogName;
    // Bloodrage: instant 10 Rage on cast, costs
    // health. Always procs (100%, no ICD - has
    // its own 60s shared cooldown).
    c.entries.push_back(makeRule(
        20, "Bloodrage Instant Rage",
        2687 /* Bloodrage spell */,
        14201 /* Rage gain effect */,
        P::OnCast,
        10000 /* 100% */,
        0,
        0));
    // Berserker Rage: 100% on cast, immunity to
    // fear/sap/incapacitate. Uses the OnCast event
    // for bookkeeping.
    c.entries.push_back(makeRule(
        21, "Berserker Rage CC Immune",
        18499 /* Berserker Rage spell */,
        23691 /* Berserker Rage CC-immunity aura
                 effect - distinct spellId so the
                 OnCast trigger does NOT recurse
                 into the source spell */,
        P::OnCast,
        10000 /* 100% */,
        0,
        0));
    // Anger Management: passive talent. OnDodge
    // generates 2 Rage. 100%, no ICD.
    c.entries.push_back(makeRule(
        22, "Anger Management Dodge Rage",
        12296 /* Anger Mgmt talent */,
        14201 /* Rage gain effect */,
        P::OnDodge,
        10000 /* 100% */,
        0,
        0));
    return c;
}

} // namespace pipeline
} // namespace wowee
