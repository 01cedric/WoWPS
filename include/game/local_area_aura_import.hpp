#pragma once
#include "game/local_area_aura.hpp"
#include "game/local_gameplay.hpp"
#include "game/local_pet_import.hpp"
#include "game/local_spell_columns.hpp"
#include <array>
#include <cmath>
#include <cstdint>

// P03/D2 source decoding for the raid area aura.
//
// Exactly one shape is admitted, and it is the one the audit reviewed in
// the source audit section 4.4:
//
//   SPELL_EFFECT_APPLY_AREA_AURA_RAID (65, SharedDefines.h:831) on every carried
//   effect, Targets::TARGET_UNIT_CASTER (1) with no secondary target, one shared
//   SpellRadius.dbc record that does not scale with level, a SpellDuration.dbc
//   record of -1 (indefinite), and a caster that is a player class.
//
// That is the 58-effect-slot / 39-spell shape of section 4.1. Inside it the only
// aura types this ruleset can honestly run are 15 SPELL_AURA_DAMAGE_SHIELD
// (SpellAuraDefines.h:78, already implemented as LocalProcEffect::MeleeDamageShield)
// and 22 SPELL_AURA_MOD_RESISTANCE (already decoded as d.buffArmor). Every other
// aura type of an admitted spell is carried ONLY as the reference's zero-amount
// marker - EffectBasePoints of -1, which SpellEffectInfo::CalcValue turns into an
// amount of zero (SpellInfo.cpp:409-448) - and a non-zero one rejects the whole
// spell rather than inventing a stat system. Carrying a marker in the effect mask
// is not a claim that its aura type is implemented; section 2.8 records exactly
// what auras 79 and 193 do, which is nothing but group membership.
//
// Everything else stays rejected with its own reason, as section 4.4 requires:
// effect 27 SPELL_EFFECT_PERSISTENT_AREA_AURA (a DynObjAura with a world object
// this model does not own), effect 35 SPELL_EFFECT_APPLY_AREA_AURA_PARTY (sub-group
// semantics and channels), effects 119/128/129/143 (pet/friend/enemy/owner
// searches with no local analogue), effect 28 totem summons (a creature spawn,
// slot arbitration and despawn that P08 owns, whose group aura is a separate
// creature-cast spell), a radius that scales with level, the source- and
// destination-anchored targets 18/21/22, and DurationIndex 0, whose lifetime is
// the owner's rather than the aura's.
//
// Admission is additionally restricted to the seven reviewed Retribution Aura
// ranks by an all-columns pin, in the same way local_pet_import.hpp pins its five
// summon rows: the shape gate above states what the ruleset can run, and the pin
// states what has actually been read column by column. Devotion Aura is the
// natural second candidate (section 4.4) and is deliberately NOT admitted here,
// because its rows have not been audited to column exactness.
namespace wowee::game {

/// SharedDefines.h effect ids. SpellInfo.cpp:378-388 IsAreaAuraEffect lists
/// 35, 65, 119, 128, 129 and 143; 27 is the DynObjAura producer.
inline constexpr uint32_t kSourceEffectPersistentAreaAura = 27;
inline constexpr uint32_t kSourceEffectApplyAreaAuraParty = 35;
inline constexpr uint32_t kSourceEffectApplyAreaAuraRaid = 65;
inline constexpr uint32_t kSourceEffectApplyAreaAuraPet = 119;
inline constexpr uint32_t kSourceEffectApplyAreaAuraFriend = 128;
inline constexpr uint32_t kSourceEffectApplyAreaAuraEnemy = 129;
inline constexpr uint32_t kSourceEffectApplyAreaAuraOwner = 143;
/// Targets, SharedDefines.h:1412-1432. These three anchor the search somewhere
/// other than the emitter and are rejected by name.
inline constexpr uint32_t kSourceTargetDestCaster = 18;
inline constexpr uint32_t kSourceTargetUnitTargetAlly = 21;
inline constexpr uint32_t kSourceTargetSrcCaster = 22;
/// SpellAuraDefines.h:78 and :85 - the only two types with local behaviour.
inline constexpr uint32_t kSourceAuraDamageShield = 15;
inline constexpr uint32_t kSourceAuraModResistance = 22;
/// SpellFamilyNames::SPELLFAMILY_SHAMAN, the family every totem summon row of
/// audit section 4.3 belongs to.
inline constexpr uint32_t kSourceSpellFamilyShaman = 11;
/// The one area-aura profile this ruleset runs: Retribution Aura's caster-
/// anchored, fixed-radius, indefinite raid shape.
inline constexpr uint8_t kLocalAreaAuraProfileRetribution = 1;
/// Player class bits of 3.3.5a: classes 1-9 and 11. Class 10 does not exist.
inline constexpr uint32_t kSourcePlayerClassMask = 0x5ffu;

/// One reviewed Retribution Aura rank, by the columns that legitimately differ
/// between the seven. 228 of the 234 columns are byte-identical across them
/// (audit section 1.1); the rest are pinned inline below. The damage-shield
/// amount is NOT normalised out of the record: it is pinned as the raw base
/// points the row carries and read back through CalcValue's DieSides rule.
struct ClientAreaAuraProfile {
    uint32_t id;         ///< Spell.dbc id
    uint32_t level;      ///< columns 38 and 39, baseLevel and spellLevel
    uint32_t basePoints; ///< column 80, EffectBasePoints[0]
    /// Column 112, EffectMiscValue[2] of the aura-193 marker. It is 127 on rank
    /// one and 0 on every later rank; the record is preserved, not tidied.
    uint32_t meleeSlowMisc;
};
inline constexpr ClientAreaAuraProfile kClientRetributionAuraProfiles[] = {
    {7294,  16,   9, 127},
    {10298, 26,  17, 0},
    {10299, 36,  26, 0},
    {10300, 46,  36, 0},
    {10301, 56,  47, 0},
    {27150, 66,  61, 0},
    {54043, 76, 111, 0},
};
inline const ClientAreaAuraProfile* clientRetributionAuraProfile(uint32_t id) {
    for (const auto& candidate : kClientRetributionAuraProfiles)
        if (candidate.id == id) return &candidate;
    return nullptr;
}

/// Decode the reviewed raid area aura. Returns true when this row is one, in
/// which case the caller skips the ordinary damage/heal/buff effect rules for
/// it; returns false otherwise, having stated a reason for every area producer
/// that is recognisably one of the rejected shapes and staying silent about
/// rows this decoder has no business speaking for.
///
/// `Tables` must supply the spell, duration and radius tables with their id
/// indices; a table view without SpellRadius.dbc rejects rather than guesses.
template <class Tables>
inline bool decodeClientAreaAuraProfile(const Tables& t, uint32_t row, LocalSpellDefinition& d) {
    const auto u = [&](uint32_t c) { return t.spells->getUInt32(row, c); };
    const auto i = [&](uint32_t c) { return t.spells->getInt32(row, c); };
    const auto f = [&](uint32_t c) { return t.spells->getFloat(row, c); };
    const auto reject = [&](const char* reason) {
        if (d.unsupportedReason.empty()) d.unsupportedReason = reason;
        return false;
    };
    // A shapeshift's effects belong to the local form rules, which the ordinary
    // effect loop already hands them; this decoder never speaks for one.
    if (d.formId || u(0) != d.id) return false;

    // Which area producer, if any, this row is. Anything outside the reviewed
    // raid shape gets its own reason, so no area producer is silently dropped.
    bool raid = false, persistent = false, party = false, foreign = false, totem = false;
    for (uint32_t effect = 0; effect < 3; ++effect) {
        switch (u(71 + effect)) {
            case kSourceEffectApplyAreaAuraRaid: raid = true; break;
            case kSourceEffectPersistentAreaAura: persistent = true; break;
            case kSourceEffectApplyAreaAuraParty: party = true; break;
            case kSourceEffectApplyAreaAuraPet:
            case kSourceEffectApplyAreaAuraFriend:
            case kSourceEffectApplyAreaAuraEnemy:
            case kSourceEffectApplyAreaAuraOwner: foreign = true; break;
            // A totem is a creature, and the aura the group receives is a
            // separate effect-65 spell that creature casts (audit 4.3). Whether
            // a summon is a totem lives in SummonProperties.dbc, which this
            // import does not load, so the whole Shaman summon family - the one
            // family every totem row belongs to - is rejected rather than
            // guessed, exactly as local_pet_import.hpp rejects effect 28.
            case kSourceEffectSummon:
                if (u(spell335::SpellFamily) == kSourceSpellFamilyShaman && u(113 + effect)) totem = true;
                break;
            default: break;
        }
    }
    if (persistent) return reject("Persistent area auras own a dynamic object and are not implemented");
    if (totem) return reject("Totem summons and the area auras their creature casts are not implemented");
    if (foreign) return reject("Pet, friend, enemy and owner area auras are not implemented");
    if (party) return reject("Party area auras are not implemented");
    if (!raid) return false;

    // Every carried effect is the reviewed raid effect, anchored on the caster,
    // with no secondary target and one shared radius record.
    uint32_t radiusIndex = 0;
    uint8_t mask = 0;
    for (uint32_t effect = 0; effect < 3; ++effect) {
        const auto type = u(71 + effect);
        if (!type) continue;
        if (type != kSourceEffectApplyAreaAuraRaid)
            return reject("Area aura mixed with an unreviewed effect is not implemented");
        const auto target = u(86 + effect);
        if (target == kSourceTargetDestCaster || target == kSourceTargetUnitTargetAlly ||
            target == kSourceTargetSrcCaster)
            return reject("Source- and destination-anchored area auras are not implemented");
        if (target != kSourceTargetUnitCaster)
            return reject("Unreviewed area aura implicit target is not implemented");
        if (u(89 + effect)) return reject("Secondary area aura targeting is not implemented");
        if (radiusIndex && u(92 + effect) != radiusIndex)
            return reject("Per-effect area aura radii are not implemented");
        radiusIndex = u(92 + effect);
        mask = uint8_t(mask | (1u << effect));
    }
    if (!mask || (mask & ~kLocalAreaAuraEffectMaskAll)) return reject("Unreviewed area aura effect mask");
    // No effect of an admitted spell carries a per-effect class mask; one would
    // select which other spells the aura modifies, which this ruleset cannot do.
    for (uint32_t column = spell335::EffectClassMask; column < spell335::EffectClassMask + 9; ++column)
        if (u(column)) return reject("Per-effect area aura class masks are not implemented");

    // Fixed radius only, the same rule the importer already applies to the
    // arcane area profile: no per-level growth and no minimum/maximum spread.
    const auto radiusRow = radiusIndex ? Tables::lookup(t.radiusIndex, radiusIndex) : -1;
    if (!t.radii || radiusRow < 0) return reject("Area aura radius record missing");
    const auto radius = t.radii->getFloat(uint32_t(radiusRow), 1);
    const auto radiusPerLevel = t.radii->getFloat(uint32_t(radiusRow), 2);
    const auto radiusMax = t.radii->getFloat(uint32_t(radiusRow), 3);
    if (!std::isfinite(radius) || radius <= 0 || radius > kLocalAreaAuraMaxRadius ||
        radiusPerLevel != 0 || radiusMax != radius)
        return reject("Variable or unsupported area aura radius");

    // SpellDuration.dbc -1: the emitter lives until it is replaced or cancelled.
    // A DurationIndex of 0 is the owner-bound passive lifetime of the totem
    // effect-spells and of the passive auras, which is a different lifecycle.
    if (!u(40)) return reject("Area auras without a duration record are owner-bound and not implemented");
    const auto durationRow = Tables::lookup(t.durationIndex, u(40));
    if (durationRow < 0) return reject("Area aura duration record missing");
    if (t.durations->getInt32(uint32_t(durationRow), 1) != -1 ||
        t.durations->getInt32(uint32_t(durationRow), 2) != 0 ||
        t.durations->getInt32(uint32_t(durationRow), 3) != -1)
        return reject("Only the indefinite area aura duration is implemented");

    // The emitter is a player. A creature-cast area aura needs the NPC aura
    // storage and lifecycle this package does not own.
    if (!d.allowableClasses || (d.allowableClasses & ~kSourcePlayerClassMask))
        return reject("Area aura caster is not a player class");

    std::array<uint8_t, 3> types{};
    std::array<int32_t, 3> amounts{};
    std::array<uint32_t, 3> miscValues{};
    for (uint32_t effect = 0; effect < 3; ++effect) {
        if (!(mask & (1u << effect))) continue;
        const auto aura = u(95 + effect);
        if (aura > 255) return reject("Unreviewed area aura type");
        // SpellEffectInfo::CalcValue (SpellInfo.cpp:409-448): with DieSides == 1
        // the amount is exactly BasePoints + 1 and nothing scales it, so a
        // per-level term, a die range, a periodic tick, a chain, an item, a
        // trigger or a combo term would all be amounts this cannot reproduce.
        if (i(74 + effect) != 1 || f(77 + effect) != 0 || u(83 + effect) ||
            u(98 + effect) || f(spell335::EffectValueMultiplier + effect) != 0 ||
            u(104 + effect) || u(107 + effect) || u(113 + effect) || u(116 + effect) ||
            f(119 + effect) != 0)
            return reject("Scaling, periodic or triggered area aura effects are not implemented");
        const auto base = i(80 + effect);
        if (base < -1 || base > 100000) return reject("Invalid area aura effect amount");
        const int32_t amount = base + 1;
        const bool behaviour = aura == kSourceAuraDamageShield || aura == kSourceAuraModResistance;
        // Only the damage shield and the resistance aura have local behaviour.
        // Every other type is admitted only as the reference's zero-amount
        // marker, whose base points are -1 (audit 1.6 correction 1).
        if (!behaviour && base != -1) return reject("Area aura type has no local behaviour");
        if (behaviour && amount <= 0) return reject("Reviewed area aura effect carries no amount");
        // One emitter record holds one amount and it is effect zero's, so a
        // later slot with an amount is data this ruleset would have to drop.
        if (effect && amount) return reject("Only the first area aura effect may carry an amount");
        types[effect] = uint8_t(aura);
        amounts[effect] = amount;
        miscValues[effect] = u(110 + effect);
    }
    // A spell of markers alone has no observable behaviour at all; admitting it
    // would put an emitter on the paladin that does nothing.
    if (!(mask & 1) || !amounts[0]) return reject("Area aura carries no implemented effect");

    // Column pin. The shape gate above says what the ruleset can run; this says
    // what has actually been read, column by column, out of the player's own
    // Spell.dbc. Columns 131-203 are presentation. EffectBasePoints[1..2] are
    // pinned as -1 rather than 0 and EffectMiscValue[2] as 127 only on rank one:
    // the record is validated as written, never as tidied.
    const auto* profile = clientRetributionAuraProfile(d.id);
    if (!profile) return reject("Unreviewed area aura source profile");
    if (!matchesClientSourceColumns(t, row,
        {{4, 151322624u}, {6, 17u}, {7, 1114112u}, {8, 3145728u}, {10, 1073741824u}, {11, 4u},
         {28, 1u}, {35, 101u}, {38, profile->level}, {39, profile->level}, {40, 21u}, {46, 1u},
         {68, 4294967295u},
         {71, kSourceEffectApplyAreaAuraRaid}, {72, kSourceEffectApplyAreaAuraRaid},
         {73, kSourceEffectApplyAreaAuraRaid}, {74, 1u}, {75, 1u}, {76, 1u},
         {80, profile->basePoints}, {81, 4294967295u}, {82, 4294967295u},
         {86, kSourceTargetUnitCaster}, {87, kSourceTargetUnitCaster}, {88, kSourceTargetUnitCaster},
         {92, 23u}, {93, 23u}, {94, 23u},
         {95, kSourceAuraDamageShield}, {96, 79u}, {97, 193u},
         {111, 127u}, {112, profile->meleeSlowMisc},
         {205, 133u}, {206, 1500u}, {208, 10u}, {209, 8u}, {211, 32u},
         {213, 1u}, {214, 1u}, {215, 1u},
         {216, 1065353216u}, {217, 1065353216u}, {218, 1065353216u},
         {225, 2u}, {229, 1023879938u}}))
        return reject("Unreviewed Retribution Aura source columns");

    d.areaAuraProfile = kLocalAreaAuraProfileRetribution;
    d.areaAuraEffectMask = mask;
    d.areaAuraTypes = types;
    d.areaAuraAmounts = amounts;
    d.areaAuraMiscValues = miscValues;
    d.areaAuraRadius = radius;
    // The emitter is not a lease: SpellDuration.dbc row 21 is -1, so the local
    // duration stays zero and the indefinite flag carries the source fact.
    d.indefiniteDuration = true;
    d.durationMs = 0;
    return true;
}
}
