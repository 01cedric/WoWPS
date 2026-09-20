#pragma once
#include "game/local_gameplay.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// P03/D2: raid area auras as a real emitter/recipient pair.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   SharedDefines.h:831  SPELL_EFFECT_APPLY_AREA_AURA_RAID (65).
//   SpellAuras.cpp UnitAura::FillTargetMap / Aura::UpdateTargetMap - one Aura on
//                        the caster produces many AuraApplications; the caster's
//                        own application is unconditional.
//   Unit.cpp::IsHighestExclusiveAuraEffect (4311-4348), :4329 - an area aura is
//                        never removed from its original owner, because that
//                        cancels the whole aura. Emitter and derived application
//                        are therefore two different records, not one.
//   SpellAuras.cpp:532-576 IsPaladinAuraDominant / GetDominantOtherSameSpellApp -
//                        many sources coexist on one recipient and only the
//                        strongest keeps its effects; the loser's application
//                        stays alive with an empty effect mask.
//   SpellInfo.cpp:1449-1466, :2201-2203 - SPELL_SPECIFIC_AURA exclusivity per
//                        caster, keyed on SpellFamilyFlags[2] & 0x20.
//   Unit.cpp::DealMeleeDamage (2126-2128) -> DealDamageShieldDamage (2131-2183) -
//                        aura 15 does NOT retaliate through the proc path, and
//                        :2181 credits the RECIPIENT as the damage source.
//
// Scope is deliberately bounded and stated in the source audit:
// this is the caster-anchored, fixed-radius, indefinite raid-aura shape only.
// Persistent area auras (effect 27), totems, channels and effect 35 as a class
// stay rejected and remain P08.
namespace wowee::game {

/// Bounds. An emitter is one active area aura the owner projects; a recipient
/// holds one derived application per distinct source.
inline constexpr size_t kLocalMaxAreaAuraEmitters = 4;
inline constexpr size_t kLocalMaxAreaAuraApplications = 8;
/// The reviewed shape carries three source effects at most.
inline constexpr uint8_t kLocalAreaAuraEffectMaskAll = 0x7;
/// SpellRadius.dbc records above this are not part of the reviewed shape.
inline constexpr float kLocalAreaAuraMaxRadius = 100.0f;
/// Reconciliation cadence. The reference reconciles on every aura update; this
/// ruleset recomputes on a fixed interval and on every emitter change.
inline constexpr uint32_t kLocalAreaAuraReconcileMs = 500;

inline bool validLocalAreaAuraEmitter(const LocalAreaAuraEmitter& e) {
    return e.spellId && e.effectMask && !(e.effectMask & ~kLocalAreaAuraEffectMaskAll) &&
           e.amount <= 100000 && e.instanceId <= 65535;
}

inline bool validLocalAreaAuraEmitters(const std::vector<LocalAreaAuraEmitter>& rows) {
    if (rows.size() > kLocalMaxAreaAuraEmitters) return false;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!validLocalAreaAuraEmitter(rows[i])) return false;
        // SpellInfo::IsAuraExclusiveBySpecificPerCasterWith: one specific aura
        // per caster. Two emitters of the same spell are never both live.
        for (size_t j = 0; j < i; ++j) if (rows[i].spellId == rows[j].spellId) return false;
    }
    return true;
}

inline bool validLocalAreaAuraApplications(const std::vector<LocalAreaAuraApplication>& rows) {
    if (rows.size() > kLocalMaxAreaAuraApplications) return false;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& a = rows[i];
        if (!a.spellId || !a.emitterGuid || (a.effectMask & ~kLocalAreaAuraEffectMaskAll)) return false;
        if (a.effective && !a.effectMask) return false;
        if (a.amount > 100000 || a.instanceId > 65535) return false;
        // One application per (source, spell): a second is the same source twice.
        for (size_t j = 0; j < i; ++j)
            if (rows[i].emitterGuid == rows[j].emitterGuid && rows[i].spellId == rows[j].spellId) return false;
    }
    return true;
}

/// SPELL_SPECIFIC_AURA exclusivity key. The reference derives it from the spell
/// family and family flags (SpellInfo.cpp:2201-2203), not from the spell id, so
/// every rank of one aura shares a key and two different auras do not.
struct LocalAreaAuraGroup {
    uint32_t family = 0;
    std::array<uint32_t, 3> familyFlags{};
    bool operator==(const LocalAreaAuraGroup&) const = default;
};

inline LocalAreaAuraGroup localAreaAuraGroup(const LocalSpellDefinition& d) {
    return {d.spellFamily, d.spellFamilyFlags};
}

/// SpellAuras.cpp:532-576 IsPaladinAuraDominant, in source order: larger
/// absolute primary amount wins; then the recipient's own self-cast aura; then
/// the lower caster GUID. Returns true when `candidate` beats `incumbent`.
inline bool localAreaAuraDominates(const LocalAreaAuraApplication& candidate,
                                   const LocalAreaAuraApplication& incumbent,
                                   uint64_t recipientGuid) {
    if (candidate.amount != incumbent.amount) return candidate.amount > incumbent.amount;
    const bool candidateSelf = candidate.emitterGuid == recipientGuid;
    const bool incumbentSelf = incumbent.emitterGuid == recipientGuid;
    if (candidateSelf != incumbentSelf) return candidateSelf;
    return candidate.emitterGuid < incumbent.emitterGuid;
}

/// The strongest effective damage-shield amount a recipient currently carries.
/// Zero when it holds none: an application whose effects were stripped by a
/// dominant neighbour retaliates for nothing.
inline uint32_t localAreaAuraShieldAmount(const std::vector<LocalAreaAuraApplication>& rows,
                                          uint32_t mapId, uint32_t instanceId) {
    uint32_t best = 0;
    for (const auto& a : rows)
        if (a.effective && (a.effectMask & 1) && a.mapId == mapId && a.instanceId == instanceId)
            best = std::max(best, a.amount);
    return best;
}
}
