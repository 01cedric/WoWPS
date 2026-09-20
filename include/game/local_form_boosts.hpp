#pragma once
#include "game/local_forms.hpp"
#include "game/local_armor.hpp"
#include "game/local_talents.hpp"
#include <algorithm>

// P06 . The per-form modifiers this build did not have, read from the
// boost spells the client's own Spell.dbc carries rather than transcribed as
// constants.
//
// The reference applies them from AuraEffect::HandleShapeshiftBoosts
// (SpellAuraEffects.cpp:1350-1445), which casts a form's boost spells when the
// form's aura 36 is applied and removes them when it is lost. Nothing here is
// stored on the player: every helper reads the ACTIVE form and returns zero
// without one, so leaving a form returns every number to its caster value by
// construction and there is no state a transition could fail to clear. That is
// the criterion's "without retaining old-form effects" clause, expressed as an
// absence rather than a sweep - and it is why the reference's own
// Aura::IsRemovedOnShapeLost sweep (SpellAuras.cpp:1131, driven from
// SpellAuraEffects.cpp:1612-1637) has nothing to do here.
namespace wowee::game {

/// Call `fn` with every accepted boost definition the active form grants.
template <class Fn>
inline void localForEachFormBoost(const LocalRealmPlayer& p, const LocalWorldContent& c, Fn&& fn) {
    const auto* form = localActiveForm(p);
    if (!form || p.classId < 1 || p.classId > 11) return;
    const uint64_t mask = uint64_t(1) << (form->form - 1);
    for (const auto& boost : kLocalFormBoosts) {
        if (!(boost.forms & mask)) continue;
        const auto* d = c.spell(boost.spell);
        if (!d || !d->clientSpell || !d->passive || !d->unsupportedReason.empty()) continue;
        if (!(d->allowableClasses & (1u << (p.classId - 1)))) continue;
        fn(*d);
    }
}

/// Aura 99, summed over the form's boosts and scaled by CalcValue's level term.
inline uint32_t localFormBoostAttackPower(const LocalRealmPlayer& p, const LocalWorldContent& c) {
    uint64_t total = 0;
    localForEachFormBoost(p, c, [&](const LocalSpellDefinition& d) {
        if (!d.passiveAttackPower && d.passiveAttackPowerPerLevel == 0) return;
        // localMeleeStats bounds its own level terms to [1, 80]; the boost uses
        // the same bound so a level the class table cannot answer for cannot
        // scale one term and not the other.
        total += localFormBoostAmount(std::clamp<uint32_t>(p.level, 1, 80), d.passiveAttackPower,
                                      d.passiveAttackPowerPerLevel, d.baseLevel, d.maxLevel, d.spellLevel);
    });
    return uint32_t(std::min<uint64_t>(total, 1000000));
}

/// Aura 137 with misc 2 (STAT_STAMINA). The reference multiplies; so does the
/// talent path (`localTalentTotalStat`), and this composes with it the same way.
inline float localFormBoostStatMultiplier(const LocalRealmPlayer& p, const LocalWorldContent& c, size_t stat) {
    float multiplier = 1;
    if (stat != 2) return multiplier;
    localForEachFormBoost(p, c, [&](const LocalSpellDefinition& d) {
        if (d.passiveTotalStatPct[2]) multiplier *= 1 + float(d.passiveTotalStatPct[2]) / 100;
    });
    return multiplier;
}

/// Aura 52 on a boost carries no equipped-item requirement (`EquippedItemClass`
/// is -1 on all five), so unlike the talent path it applies to every hand,
/// including an unarmed form attack.
inline uint32_t localFormBoostCritPct(const LocalRealmPlayer& p, const LocalWorldContent& c) {
    uint32_t percent = 0;
    localForEachFormBoost(p, c, [&](const LocalSpellDefinition& d) {
        percent = std::min(100u, percent + uint32_t(d.passiveMeleeCritPct));
    });
    return percent;
}

/// Aura 280. Battle Stance carries it on the form spell itself rather than on a
/// boost (2457 slot 1, EffectBasePoints 9 -> +10%), which the importer has read
/// and asserted previously and discarded; both sources are summed here.
inline uint32_t localFormArmorPenetrationPct(const LocalRealmPlayer& p, const LocalWorldContent& c) {
    uint32_t percent = 0;
    if (const auto* form = localActiveForm(p))
        if (const auto* d = c.spell(form->spell); d && d->unsupportedReason.empty())
            percent += d->passiveArmorPenetrationPct;
    localForEachFormBoost(p, c, [&](const LocalSpellDefinition& d) {
        percent += d.passiveArmorPenetrationPct;
    });
    return std::min(100u, percent);
}

// ---------------------------------------------------------------------------
// The entry-resource talents (P06 / the implementation, checkpoint C3).
// ---------------------------------------------------------------------------

/// Furor's chance, `GetDummyAuraEffect(SPELLFAMILY_DRUID, 238, 0)->GetAmount()`
/// at SpellAuraEffects.cpp:2103. One rank is allocated at a time, so the
/// reference's single effect is the strongest allocated rank here.
inline uint32_t localFurorChancePct(const LocalRealmPlayer& p, const LocalWorldContent& c) {
    if (p.dead || p.classId < 1 || p.classId > 11 || !validLocalTalents(p)) return 0;
    uint32_t chance = 0;
    for (auto [id, rank] : p.talents) {
        const auto* s = localTalentSpell(c, id, rank);
        if (!s || !s->passive || !s->unsupportedReason.empty() || !s->furorChancePct) continue;
        if (!(s->allowableClasses & (1u << (p.classId - 1)))) continue;
        if (!localTalentPrerequisitesReady(p, c, *s)) continue;
        chance = std::max(chance, uint32_t(s->furorChancePct));
    }
    return std::min(100u, chance);
}

/// The rage a stance change keeps. SpellAuraEffects.cpp:2197-2221 walks the
/// spellbook AND the talent map for SpellFamilyName == SPELLFAMILY_WARRIOR with
/// SpellIconID == 139 and sums `CalculateSpellDamage(..., 0) * 10` tenths; the
/// client's data has exactly four such rows - Stance Mastery 12678, a trainer
/// spell with talentId 0, and the three Tactical Mastery ranks. The two loops
/// are split here by that same talentId so a spell in both cannot count twice.
inline uint32_t localRetainedRage(const LocalRealmPlayer& p, const LocalWorldContent& c) {
    if (p.dead || p.classId != 1) return 0;
    uint64_t rage = 0;
    for (auto id : p.knownSpells) {
        const auto* s = c.spell(id);
        if (!s || s->talentId || !s->passive || !s->unsupportedReason.empty() || !s->retainedRage) continue;
        if (!(s->allowableClasses & 1u)) continue;
        rage += s->retainedRage;
    }
    if (validLocalTalents(p))
        for (auto [id, rank] : p.talents) {
            const auto* s = localTalentSpell(c, id, rank);
            if (!s || !s->passive || !s->unsupportedReason.empty() || !s->retainedRage) continue;
            if (!(s->allowableClasses & 1u) || !localTalentPrerequisitesReady(p, c, *s)) continue;
            rage += s->retainedRage;
        }
    return uint32_t(std::min<uint64_t>(rage, 100));
}

/// The resource a form entry starts with, `roll` being urand(0, 99).
///
/// Three source rules, one per power type (SpellAuraEffects.cpp:2088-2135 and
/// :2184-2223):
///   Cat          SetPower(ENERGY, 0) then energize min(oldEnergy, chance).
///   Bear/Dire    setPowerType zeroes rage, then urand(0,99) < chance casts
///                17057 for 10 rage (Effect 30 ENERGIZE, misc 1 POWER_RAGE,
///                EffectBasePoints 99 -> 100 tenths). 17057 is an internal
///                triggered spell and is never offered to the importer, exactly
///                like Ghost Wolf's 67116.
///   Stance       the OLD stance's removal clamps rage to Rage_val, so the new
///                stance starts at min(previous rage, retained).
///
/// DEVIATION, recorded in the source audit: the reference's `oldEnergy` is
/// UNIT_FIELD_POWER's energy slot, which survives a form change because
/// Unit::setPowerType's POWER_ENERGY branch (Unit.cpp:6774-6776) sets only the
/// maximum. This build has one active resource field and no persistent energy,
/// so it substitutes that field's maximum. The grant is therefore the
/// reference's value whenever the reference's field is at or above the chance,
/// and at most `chance` too generous after energy has been spent in form.
/// Carrying the field would move Save 29, which this checkpoint does not do.
inline constexpr uint32_t kLocalFurorBearRage = 10;
inline uint32_t localFormEntryResource(const LocalRealmPlayer& p, const LocalWorldContent& c,
                                       const LocalFormProfile& f, uint32_t roll) {
    if (f.power == LocalResourceType::Energy) return localFurorChancePct(p, c);
    if (f.power != LocalResourceType::Rage) return 0;
    if (p.classId == 11) {
        const auto chance = localFurorChancePct(p, c);
        return chance && roll < chance ? kLocalFurorBearRage : 0;
    }
    const uint32_t previous = p.resourceType == LocalResourceType::Rage ? p.mana : 0;
    return std::min(previous, localRetainedRage(p, c));
}

/// Unit::CalcArmorReducedDamage, Unit.cpp:2256-2267: the penetration cap is a
/// function of the VICTIM's level, the pool is capped at a third of the sum and
/// never above the armour itself, and the percentage is taken of that pool.
/// This build has no armour-penetration combat rating, so `bonusPct` is the
/// whole of the reference's `bonusPct + GetRatingBonusValue(CR_ARMOR_PENETRATION)`.
inline uint32_t localArmorAfterPenetration(uint32_t armor, uint8_t victimLevel, uint32_t bonusPct) {
    if (!armor || !bonusPct) return armor;
    const float level = float(std::clamp<uint32_t>(victimLevel, 1u, 83u));
    float maxArmorPen = level < 60 ? 400.f + 85.f * level
                                   : 400.f + 85.f * level + 4.5f * 85.f * (level - 59.f);
    maxArmorPen = std::min((float(armor) + maxArmorPen) / 3.f, float(armor));
    const float penetrated = std::min(maxArmorPen * float(bonusPct) / 100.f, maxArmorPen);
    return uint32_t(std::max(0.f, float(armor) - penetrated));
}

}  // namespace wowee::game
