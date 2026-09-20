#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

// P07 : the pinned world dump's own pet tables, compiled.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   Guardian::InitStatsForLevel   Pet.cpp:1041-1200. In source order: armour is
//       `petlevel * 50` and is then REPLACED by pet_levelstats.armor when that
//       is positive; attack time is `cinfo->BaseAttackTime` for a non-hunter pet
//       when it is at least 1000; health, mana, the five stats and - for a
//       SUMMON_PET - the weapon damage all come from the row. Without a row the
//       reference falls back to creature_classlevelstats and five hard-coded
//       stats; every creature reachable here HAS a row, so that arm is unused.
//   ObjectMgr::LoadPetLevelInfo    ObjectMgr.cpp:4206-4300. Its SELECT names the
//       columns in a different order from the schema, which is why
//       tools/local_realm/import_pet_catalog.py reads the dump by column NAME.
//   ObjectMgr::GetPetLevelInfo     ObjectMgr.cpp:4300+. Level 1 lives at [0].
//   ObjectMgr::GeneratePetName     ObjectMgr.cpp:8132-8148. One random word from
//       half 0 concatenated with one from half 1.
//   Spell::EffectSummonPet         SpellEffects.cpp:3475-3477 applies that name
//       to EVERY summoned pet, not only the ghoul.
//   Creature::SetObjectScale       Creature.cpp:3528-3550 and ObjectDefines.h:44
//       for the combat reach and bounding radius, which P05's melee gate reads.
//
// the source audit section 1.4-1.6 decodes all three tables and
// section 0.1 records why they are needed: the shipped 14,496-record world
// catalog holds only SPAWNABLE creatures, and no creature an admitted summon
// names is spawnable, so `content->npc(entry)` answered null for every one of
// them and no pet could be created at all.
namespace wowee::game {

/// One creature_template row, reduced to the columns InitStatsForLevel reads.
struct LocalPetTemplateRow {
    uint32_t entry, displayId, baseAttackTimeMs;
    uint8_t damageSchool, creatureType, family, unitClass;
    float combatReach, boundingRadius;
    const char* name;
};
inline constexpr LocalPetTemplateRow kLocalPetTemplates[] = {
#include "game/local_pet_templates_generated.inc"
};

/// One pet_levelstats row. `values` is the schema's own order, which is NOT the
/// reference query's order: hp, mana, armor, str, agi, sta, inte, spi,
/// min_dmg, max_dmg.
struct LocalPetLevelRow {
    uint32_t entry;
    uint32_t level;
    uint32_t values[10];
    constexpr uint32_t health() const { return values[0]; }
    constexpr uint32_t mana() const { return values[1]; }
    constexpr uint32_t armor() const { return values[2]; }
    /// Stats::STAT_STRENGTH..STAT_SPIRIT, the reference's own 0..4 order.
    constexpr uint32_t stat(unsigned index) const { return values[3 + (index % 5)]; }
    constexpr uint32_t minDamage() const { return values[8]; }
    constexpr uint32_t maxDamage() const { return values[9]; }
};
inline constexpr LocalPetLevelRow kLocalPetLevels[] = {
#include "game/local_pet_levels_generated.inc"
};

/// One pet_name_generation word.
struct LocalPetNameWord { uint32_t entry; uint32_t half; const char* word; };
inline constexpr LocalPetNameWord kLocalPetNameWords[] = {
#include "game/local_pet_names_generated.inc"
};

inline constexpr uint32_t kLocalPetMaxLevel = 80;
inline constexpr size_t kLocalPetTemplateCount = std::size(kLocalPetTemplates);
inline constexpr size_t kLocalPetLevelRowCount = std::size(kLocalPetLevels);
inline constexpr size_t kLocalPetNameWordCount = std::size(kLocalPetNameWords);

// The three census numbers this build was compiled against. A regenerated
// catalog that moves any of them is a deliberate act, not an accident.
static_assert(kLocalPetTemplateCount == 35, "pet_levelstats names 35 creatures");
static_assert(kLocalPetLevelRowCount == 35 * kLocalPetMaxLevel, "35 entries x 80 levels");
static_assert(kLocalPetNameWordCount == 312, "pet_name_generation has 312 words");

/// The compiled tables are emitted sorted; the lookups below binary-search them.
inline const LocalPetTemplateRow* localPetTemplate(uint32_t entry) {
    if (!entry) return nullptr;
    const auto* end = std::end(kLocalPetTemplates);
    const auto* it = std::lower_bound(std::begin(kLocalPetTemplates), end, entry,
        [](const LocalPetTemplateRow& row, uint32_t key) { return row.entry < key; });
    return it != end && it->entry == entry ? it : nullptr;
}

/// ObjectMgr::GetPetLevelInfo. A level above the table's maximum takes the
/// maximum row, which is what the reference's `level > maxlevel` clamp does
/// before the lookup; level zero has no row.
inline const LocalPetLevelRow* localPetLevelStats(uint32_t entry, uint32_t level) {
    if (!entry || !level) return nullptr;
    const auto capped = std::min(level, kLocalPetMaxLevel);
    const auto* end = std::end(kLocalPetLevels);
    const auto* it = std::lower_bound(std::begin(kLocalPetLevels), end,
        (uint64_t(entry) << 32) | capped,
        [](const LocalPetLevelRow& row, uint64_t key) {
            return ((uint64_t(row.entry) << 32) | row.level) < key;
        });
    return it != end && it->entry == entry && it->level == capped ? it : nullptr;
}

/// Guardian::InitStatsForLevel's armour rule, in its source order: the flat
/// `petlevel * 50` default, replaced by the row's own armour when that is
/// positive (Pet.cpp:1101, :1137-1138).
inline uint32_t localPetArmor(const LocalPetLevelRow* row, uint32_t level) {
    const uint32_t flat = std::min<uint32_t>(level, kLocalPetMaxLevel) * 50u;
    return row && row->armor() ? row->armor() : flat;
}

/// The half-list of generated-name words for one creature, as a [begin,end)
/// range into the compiled table. Empty when the entry has no words, which is
/// GeneratePetName's fallback condition.
inline std::pair<const LocalPetNameWord*, const LocalPetNameWord*>
localPetNameHalf(uint32_t entry, uint32_t half) {
    const auto key = [](uint32_t e, uint32_t h) { return (uint64_t(e) << 32) | h; };
    const auto* end = std::end(kLocalPetNameWords);
    const auto less = [&](const LocalPetNameWord& row, uint64_t k) {
        return key(row.entry, row.half) < k;
    };
    const auto* first = std::lower_bound(std::begin(kLocalPetNameWords), end, key(entry, half), less);
    const auto* last = std::lower_bound(first, end, key(entry, half) + 1, less);
    return {first, last};
}

/// ObjectMgr::GeneratePetName, which is two independent `urand(0, size - 1)`
/// draws - one per half - so this takes two rolls rather than deriving the
/// second from the first. Each is reduced modulo its own half's size. Returns
/// an empty string when either half is empty; the reference then falls back to
/// the family name and then to the template name, and localPetName() below
/// applies that fallback.
inline std::string localPetGeneratedName(uint32_t entry, uint32_t firstRoll, uint32_t secondRoll) {
    const auto [firstBegin, firstEnd] = localPetNameHalf(entry, 0);
    const auto [secondBegin, secondEnd] = localPetNameHalf(entry, 1);
    const auto firstCount = size_t(firstEnd - firstBegin);
    const auto secondCount = size_t(secondEnd - secondBegin);
    if (!firstCount || !secondCount) return {};
    return std::string(firstBegin[firstRoll % firstCount].word) +
           secondBegin[secondRoll % secondCount].word;
}

/// The name a freshly summoned pet is given: the generated one when the entry
/// has both halves, else the compiled template name, else empty.
inline std::string localPetName(uint32_t entry, uint32_t firstRoll, uint32_t secondRoll) {
    auto generated = localPetGeneratedName(entry, firstRoll, secondRoll);
    if (!generated.empty()) return generated;
    const auto* row = localPetTemplate(entry);
    return row && row->name ? std::string(row->name) : std::string();
}

/// How many distinct names an entry can produce - the product of its two
/// halves, which is what makes a collision between two live pets unlikely
/// rather than impossible. Zero when the entry has no generated names.
inline size_t localPetNameCombinations(uint32_t entry) {
    const auto [firstBegin, firstEnd] = localPetNameHalf(entry, 0);
    const auto [secondBegin, secondEnd] = localPetNameHalf(entry, 1);
    return size_t(firstEnd - firstBegin) * size_t(secondEnd - secondBegin);
}
}
