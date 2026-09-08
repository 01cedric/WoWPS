#pragma once

/**
 * quest_query_layout.hpp - where the strings begin in SMSG_QUEST_QUERY_RESPONSE.
 *
 * The response is a fixed block of four-byte fields followed by five strings:
 * Title, Objectives, Details, AreaDescription, CompletedText, in that order on
 * every expansion. Only the block's length changes, so its length is the whole
 * of knowing where the text starts - and reading from the wrong place does not
 * fail, it returns the wrong string.
 */

#include <cstddef>

namespace wowee {
namespace game {

/// The number of four-byte fields before the first string on WotLK.
///
/// Counted off AzerothCore's own writer, Quest::BuildQuestData:
///
///     5  id, method, level, minLevel, zoneOrSort
///     2  type, suggestedPlayers
///     4  two reputation objectives, faction and value each
///     2  nextQuestInChain, xpId
///     1  money - one field whichever branch of the hidden-rewards test runs
///     3  moneyMaxLevel, rewSpell, rewSpellCast
///     2  honorAddition, honorMultiplier
///     7  srcItem, flags, charTitle, playersSlain, bonusTalents, arenaPoints,
///        reviewRepShowMask
///    20  four rewards and six choices, id and count each
///    15  five reward factions: id, value, override
///     4  POI continent, x, y, pointOpt
///   ----
///    65
///
/// The array sizes are QUEST_REWARDS_COUNT 4, QUEST_REWARD_CHOICES_COUNT 6 and
/// QUEST_REPUTATIONS_COUNT 5, from QuestDef.h.
constexpr size_t kWotlkQuestQueryFields = 65;

/// The byte the title starts at on WotLK.
constexpr size_t kWotlkQuestQueryStringsOffset = kWotlkQuestQueryFields * 4;

}  // namespace game
}  // namespace wowee
