#pragma once

/// The gathering spells, and which rank of one a player knows.
///
/// Mining and Herb Gathering each have five ranks, and the id of every one of
/// them was written out four times: twice in spell_handler, twice more in
/// game_handler_callbacks, once as the two lists joined together. Ranks are
/// the kind of list that grows - Northrend added the fifth to each - and a
/// list that gains a rank in one copy and not the others fails quietly: the
/// node is swung at with a rank the check does not recognise, so the client
/// decides the player cannot gather and says nothing about why.
///
/// Ordered lowest rank first, which is what makes "the best rank known" a
/// backwards walk.

#include <cstddef>
#include <cstdint>

namespace wowee::game {

/// Mining, Apprentice through Grand Master.
inline constexpr uint32_t kMiningRanks[] = {2575, 2576, 3564, 10248, 29354};
/// Herb Gathering, the same five tiers.
inline constexpr uint32_t kHerbRanks[] = {2366, 2368, 3570, 11993, 28695};

inline constexpr size_t kMiningRankCount = sizeof(kMiningRanks) / sizeof(kMiningRanks[0]);
inline constexpr size_t kHerbRankCount = sizeof(kHerbRanks) / sizeof(kHerbRanks[0]);

/// The first rank of each, which is how a gather is named elsewhere: the
/// base id stands for the profession.
inline constexpr uint32_t kMiningBaseSpellId = kMiningRanks[0];
inline constexpr uint32_t kHerbBaseSpellId = kHerbRanks[0];

/// Whether a spell is any rank of Mining.
inline constexpr bool isMiningRank(uint32_t spellId) {
    for (uint32_t rank : kMiningRanks) {
        if (spellId == rank) return true;
    }
    return false;
}

/// Whether a spell is any rank of Herb Gathering.
inline constexpr bool isHerbRank(uint32_t spellId) {
    for (uint32_t rank : kHerbRanks) {
        if (spellId == rank) return true;
    }
    return false;
}

/// Whether a spell is any rank of either.
inline constexpr bool isGatherRank(uint32_t spellId) {
    return isMiningRank(spellId) || isHerbRank(spellId);
}

/// The ranks of the profession a base id names, or null for anything else.
inline const uint32_t* gatherRanksForBase(uint32_t baseSpellId, size_t& count) {
    if (baseSpellId == kMiningBaseSpellId) {
        count = kMiningRankCount;
        return kMiningRanks;
    }
    if (baseSpellId == kHerbBaseSpellId) {
        count = kHerbRankCount;
        return kHerbRanks;
    }
    count = 0;
    return nullptr;
}

}  // namespace wowee::game
