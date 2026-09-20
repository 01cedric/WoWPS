#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_pet.hpp"
#include "game/local_spell_columns.hpp"
#include "pipeline/dbc_loader.hpp"
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <utility>

// P03/D1 source decoding for owned creatures.
//
// Two profiles are admitted, both column-pinned against the player's own
// Spell.dbc rather than transcribed: every gameplay column of the reviewed rows
// is compared with matchesClientSourceColumns, and any deviation rejects the
// whole spell with a stated reason. The numbers the ruleset then runs on - the
// creature entry and the focus granted - are read back out of the record; the
// pins are a validation gate and never a source of data.
//
//   SPELL_EFFECT_SUMMON_PET (56)  - SharedDefines.h; Spell.cpp::EffectSummonPet.
//     Verified against the client's Spell.dbc: 51 rows carry effect 56, all of
//     them in effect slot 0 and none of them with a trigger spell, proc family
//     or chain target. Their implicit target is Targets::TARGET_DEST_CASTER_SUMMON
//     (32) - the summon spawn point beside the caster - and NOT
//     TARGET_UNIT_CASTER (1); 34 rows share that shape with a nonzero creature
//     entry and the infinite SpellDuration row 21, the remaining 17 differ by
//     implicit target, spawn radius or a zero entry.
//     Admission is restricted to the five rows reviewed here, which are also
//     the only effect-56 rows any class SkillLineAbility line teaches with a
//     creature entry to summon: 688, 691, 697, 712 and 30146. Call Pet (883) is
//     taught by the Hunter line but names no creature - its EffectMiscValue is
//     zero because the beast comes from the stable, which P07 owns - and the
//     other 45 rows are quest, test and creature scripts no class line teaches.
//     Effect 28 (SPELL_EFFECT_SUMMON) is NOT admitted: its guardian/totem
//     classification lives in SummonProperties.dbc, which the supplied client
//     data set does not contain, and guessing it would invent a lifecycle P08
//     owns.
//
//   Go for the Throat (34950/34954) - spell_hunter.cpp::spell_hun_go_for_the_throat.
//     A ranged critical trigger on the hunter that energizes the owned pet's
//     focus. CheckProc requires an actual pet; HandleEffectProc adds the aura
//     effect amount to POWER_FOCUS. Verified against the client's own records:
//     Talent.dbc 1818 (tab 363, Marksmanship - not Beast Mastery) carries both
//     ranks, the parent aura effect's own base points are -1 (an amount of
//     zero), and the focus actually granted is the base amount of the triggered
//     child energize, 34952 for rank 1 and 34953 for rank 2, which targets
//     Targets::TARGET_UNIT_PET (5) with EffectMiscValue POWER_FOCUS. The
//     client's ProcFlags word is 0x40140: the two ranged done bits plus
//     PROC_FLAG_DONE_PERIODIC. Only the ranged done events are reviewed as
//     recipients of this talent, so the runtime filter is the record's own word
//     narrowed to them, matching the reference spell_proc row for this family;
//     the periodic bit is inert in the source because the aura additionally
//     carries an EffectSpellClassMask, which this ruleset does not yet apply.
namespace wowee::game {

/// Targets::TARGET_UNIT_CASTER / TARGET_UNIT_PET / TARGET_DEST_CASTER_SUMMON
/// from the 3.3.5 implicit target enum.
inline constexpr uint32_t kSourceTargetUnitCaster = 1;
inline constexpr uint32_t kSourceTargetUnitPet = 5;
inline constexpr uint32_t kSourceTargetDestCasterSummon = 32;
/// SharedDefines.h effect ids used here.
inline constexpr uint32_t kSourceEffectApplyAura = 6;
inline constexpr uint32_t kSourceEffectSummon = 28;
inline constexpr uint32_t kSourceEffectEnergize = 30;
inline constexpr uint32_t kSourceEffectSummonPet = 56;
/// SPELL_AURA_PROC_TRIGGER_SPELL.
inline constexpr uint32_t kSourceAuraProcTriggerSpell = 42;
/// Powers::POWER_FOCUS.
inline constexpr uint8_t kSourcePowerFocus = 2;
/// ProcFlags ranged done bits: DONE_RANGED_AUTO_ATTACK | DONE_SPELL_RANGED_DMG_CLASS.
inline constexpr uint32_t kSourceProcFlagsRangedDone = 0x40u | 0x100u;
/// PROC_FLAG_DONE_PERIODIC. The client's own word for this talent carries it,
/// but the reviewed recipient set is the ranged one; see the note above.
inline constexpr uint32_t kSourceProcFlagsDonePeriodic = 0x40000u;
/// Talent.dbc row that teaches both Go for the Throat ranks.
inline constexpr uint32_t kSourceGoForTheThroatTalentId = 1818;

struct ClientSpellTables;

/// The same all-columns comparison detail::matchesClientTalentSource performs
/// for talent sources, written as a template so the two decoders below keep
/// working with any source table view - the import's own tables, and a test
/// harness's patched one. Every column named here must carry the listed value
/// and every column not named must be zero, so an unreviewed cost, condition,
/// secondary effect or attribute rejects the whole row. Columns 131-203 are
/// presentation (visual, name, rank and their locale masks), never gameplay.
template <class Tables>
inline bool matchesClientSourceColumns(const Tables& t, uint32_t row,
        std::initializer_list<std::pair<uint32_t, uint32_t>> populated) {
    for (uint32_t col = 1; col < 234; ++col) {
        if (col >= 131 && col <= 203) continue;
        uint32_t expected = 0;
        for (const auto& [key, value] : populated) if (key == col) { expected = value; break; }
        if (t.spells->getUInt32(row, col) != expected) return false;
    }
    return true;
}

/// One reviewed SPELL_EFFECT_SUMMON_PET row, by the columns that legitimately
/// differ between them. Everything else is identical across the five and is
/// pinned inline below. The creature entry is deliberately NOT here: it is the
/// one number the ruleset runs on, and it is read back out of the record.
struct ClientSummonPetProfile {
    uint32_t id;             ///< Spell.dbc id
    uint32_t attributesEx5;  ///< column 9
    uint32_t level;          ///< columns 38 and 39
    uint32_t reagent;        ///< column 52, the reagent item this rank names
    uint32_t reagentCount;   ///< column 60
    uint32_t basePoints;     ///< column 80
    uint32_t manaPercent;    ///< column 204
};
inline constexpr ClientSummonPetProfile kClientSummonPetProfiles[] = {
    {688,    0,  1,    0, 0, 4294967295u, 64},  // Summon Imp
    {697,    2, 10, 6265, 1,           0, 80},  // Summon Voidwalker
    {712,    2, 20, 6265, 1,           0, 80},  // Summon Succubus
    {691,    2, 30, 6265, 1,           0, 80},  // Summon Felhunter
    {30146,  2, 50, 6265, 1,           0, 80},  // Summon Felguard
};
inline const ClientSummonPetProfile* clientSummonPetProfile(uint32_t id) {
    for (const auto& candidate : kClientSummonPetProfiles)
        if (candidate.id == id) return &candidate;
    return nullptr;
}

/// Decode a summon whose only effect creates the caster's controlled pet.
/// Returns true when this spell is such a summon; the caller then skips the
/// ordinary damage/heal/buff effect rules for it.
template <class Tables>
inline bool decodeClientSummonPetProfile(const Tables& t, uint32_t row, LocalSpellDefinition& d) {
    const auto u = [&](uint32_t c) { return t.spells->getUInt32(row, c); };
    const auto* profile = clientSummonPetProfile(d.id);
    if (!profile || u(0) != d.id) return false;
    // One summon effect and nothing beside it.
    unsigned summonSlot = 3;
    for (uint32_t effect = 0; effect < 3; ++effect) {
        const auto type = u(71 + effect);
        if (!type) continue;
        // SPELL_EFFECT_SUMMON needs its SummonProperties record to say whether
        // the result is a guardian, a totem or a pet; that table is not in the
        // supplied client data, so the effect is rejected rather than guessed.
        if (type == kSourceEffectSummon) return false;
        if (type != kSourceEffectSummonPet) return false;
        if (summonSlot != 3) return false; // A second summon effect is not a reviewed shape.
        summonSlot = effect;
    }
    // Every reviewed row summons out of effect slot zero.
    if (summonSlot != 0) return false;
    // The creature entry is the record's own; it is echoed into the pin below
    // so that every other column is still compared against the reviewed shape.
    const auto entry = u(110 + summonSlot);
    if (!entry) return false;
    // Caster-summon targeted, no secondary target, no spawn radius, no chained
    // trigger, no proc family, no charges, no reagent beyond the named one and
    // no second or third effect. Presentation columns 131-203 are not gameplay.
    if (!matchesClientSourceColumns(t, row,
        {{4, 65536u}, {5, 131073u}, {9, profile->attributesEx5}, {28, 7u}, {31, 15u},
         {35, 101u}, {38, profile->level}, {39, profile->level}, {40, 21u}, {46, 1u},
         {52, profile->reagent}, {60, profile->reagentCount}, {68, 4294967295u},
         {71, kSourceEffectSummonPet}, {74, 1u}, {80, profile->basePoints},
         {86, kSourceTargetDestCasterSummon}, {110, entry}, {204, profile->manaPercent},
         {205, 133u}, {206, 1500u}, {208, 5u}, {209, 536870912u}, {213, 1u}, {214, 1u},
         {216, 1065353216u}, {217, 1065353216u}, {218, 1065353216u}, {225, 32u}}))
        return false;
    // SPELL_EFFECT_SUMMON_PET keeps the summon until it is dismissed, so the
    // record's own SpellDuration row has to be the infinite one; a finite one
    // would be a guardian lifecycle this package does not own. Its cast is the
    // fixed ten-second summon and its range record is the self range.
    const auto duration = Tables::lookup(t.durationIndex, u(40));
    const auto cast = Tables::lookup(t.castIndex, u(28));
    const auto range = Tables::lookup(t.rangeIndex, u(46));
    if (duration < 0 || cast < 0 || range < 0 ||
        t.durations->getInt32(duration, 1) != -1 || t.durations->getInt32(duration, 2) ||
        t.durations->getInt32(duration, 3) != -1 ||
        t.casts->getInt32(cast, 1) != 10000 || t.casts->getInt32(cast, 2) ||
        t.casts->getInt32(cast, 3) != 10000 ||
        t.ranges->getFloat(range, 1) != 0 || t.ranges->getFloat(range, 2) != 0 ||
        t.ranges->getFloat(range, 3) != 0 || t.ranges->getFloat(range, 4) != 0 ||
        t.ranges->getUInt32(range, 5))
        return false;
    d.summonPetEntry = entry;
    d.summonPetKind = uint8_t(LocalPetKind::Controlled);
    d.summonPetEffectSlot = uint8_t(summonSlot);
    d.summonPetDurationMs = 0;
    return true;
}

/// Decode the hunter's focus-energize proc and its internal child. `child` is
/// filled with the triggered energize so the second import pass can admit it.
template <class Tables>
inline bool decodeClientGoForTheThroat(const Tables& t, uint32_t row, LocalSpellDefinition& d,
                                       LocalSpellDefinition* child = nullptr) {
    // Talent.dbc supplies the rank and the legal route; rank one is 34950 and
    // rank two 34954. Hunter is class 3, so TalentTab.dbc gives class mask 4.
    if (d.talentId != kSourceGoForTheThroatTalentId || d.talentRank < 1 || d.talentRank > 2) return false;
    if (d.id != (d.talentRank == 1 ? 34950u : 34954u) || d.allowableClasses != (1u << 2)) return false;
    const auto u = [&](uint32_t c) { return t.spells->getUInt32(row, c); };
    if (u(0) != d.id) return false;
    const auto childId = u(116);
    if (!childId || childId == d.id) return false;
    // The client's own word is DONE_RANGED_AUTO_ATTACK | DONE_SPELL_RANGED_DMG_CLASS
    // | DONE_PERIODIC. Both ranged done bits have to be present, and nothing
    // outside that word is a reviewed event for this talent.
    const auto procFlags = u(spell335::ProcFlags);
    if ((procFlags & kSourceProcFlagsRangedDone) != kSourceProcFlagsRangedDone ||
        (procFlags & ~(kSourceProcFlagsRangedDone | kSourceProcFlagsDonePeriodic)))
        return false;
    // One aura effect only: a proc trigger at the caster with a one-sided die
    // range, no per-level scaling, no charges, no cost, no cooldown and no
    // second or third effect. The base points of that aura are -1, an amount of
    // zero: this talent's parent carries no focus of its own.
    if (!matchesClientSourceColumns(t, row,
        {{4, 464u}, {7, 67108864u}, {28, 1u}, {spell335::ProcFlags, procFlags}, {35, 100u},
         {46, 1u}, {68, 4294967295u}, {71, kSourceEffectApplyAura}, {74, 1u},
         {80, 4294967295u}, {86, kSourceTargetUnitCaster}, {95, kSourceAuraProcTriggerSpell},
         {116, childId}, {122, 2048u}, {125, 32u}, {208, 9u},
         {216, 1065353216u}, {217, 1065353216u}, {218, 1065353216u}, {225, 1u},
         {231, 1065353216u}}))
        return false;
    const auto childRow = Tables::lookup(t.spellIndex, childId);
    if (childRow < 0 || t.spells->getUInt32(uint32_t(childRow), 0) != childId) return false;
    const auto ci = [&](uint32_t c) { return t.spells->getInt32(uint32_t(childRow), c); };
    // The focus granted is the child energize's own base amount - the parent
    // aura has none - and it is read here, never written into this build. DBC
    // base points encode one less than the minimum.
    const auto childBase = ci(80);
    if (childBase < 0) return false;
    const uint32_t amount = uint32_t(childBase) + 1;
    if (amount > kLocalPetMaxFocus) return false;
    // The child is a single focus energize aimed at the caster's pet, with no
    // proc family, charges, duration, cost, cooldown or trigger of its own. The
    // amount column is echoed back so the rest of the row is still pinned.
    if (!matchesClientSourceColumns(t, uint32_t(childRow),
        {{5, 1024u}, {28, 1u}, {35, 101u}, {46, 1u}, {68, 4294967295u},
         {71, kSourceEffectEnergize}, {74, 1u}, {80, uint32_t(childBase)},
         {86, kSourceTargetUnitPet}, {110, uint32_t(kSourcePowerFocus)},
         {216, 1065353216u}, {217, 1065353216u}, {218, 1065353216u}, {225, 1u},
         {230, 1065353216u}, {231, 1065353216u}}))
        return false;
    // Both rows are instant and self-ranged, and neither carries a duration.
    const auto cast = Tables::lookup(t.castIndex, u(28));
    const auto self = Tables::lookup(t.rangeIndex, u(46));
    if (cast < 0 || self < 0 || t.casts->getInt32(cast, 1) || t.casts->getInt32(cast, 2) ||
        t.ranges->getFloat(self, 1) != 0 || t.ranges->getFloat(self, 2) != 0 ||
        t.ranges->getFloat(self, 3) != 0 || t.ranges->getFloat(self, 4) != 0 ||
        t.ranges->getUInt32(self, 5))
        return false;
    LocalProcDefinition proc;
    proc.effect = LocalProcEffect::RestorePetPower;
    proc.spellId = childId;
    // The reviewed recipients are the ranged done events; the source's periodic
    // bit is excluded rather than run without its EffectSpellClassMask.
    proc.flags = procFlags & kSourceProcFlagsRangedDone;
    proc.chance = uint8_t(std::min(100u, u(spell335::ProcChance) ? u(spell335::ProcChance) : 100u));
    proc.amount = amount;
    proc.resourceType = kSourcePowerFocus;
    proc.recipient = uint8_t(LocalProcRecipient::OwnedPet);
    proc.schoolMask = d.schoolMask ? d.schoolMask : 1;
    // Reference spell_proc row for this family selects critical results only.
    proc.hitMask = LocalProcHitCritical;
    proc.spellTypeMask = 7;
    proc.phaseMask = LocalProcPhaseHit;
    d.proc = proc;
    d.passive = true;
    d.buffSelfOnly = true;
    d.unsupportedReason.clear();
    if (child) {
        LocalSpellDefinition leaf;
        leaf.id = childId;
        leaf.clientSpell = true;
        leaf.triggeredOnly = true;
        leaf.allowableClasses = d.allowableClasses;
        leaf.procParentTalentId = d.talentId;
        leaf.resourceType = 255;
        leaf.range = 0;
        leaf.schoolMask = proc.schoolMask;
        leaf.baseLevel = 1;
        leaf.buffSelfOnly = true;
        leaf.name = t.spells->getString(uint32_t(childRow), 136);
        leaf.iconId = t.spells->getUInt32(uint32_t(childRow), 133);
        *child = std::move(leaf);
    }
    return true;
}
}
