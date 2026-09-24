#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wowee::game {
// Immutable acceptance predicates. Alternatives are OR; each alternative is
// AND. These are local state bits, not Trinity/AzerothCore QuestStatus values.
inline constexpr uint8_t LocalQuestChainNone = 1, LocalQuestChainActive = 2,
    LocalQuestChainComplete = 4, LocalQuestChainRewarded = 8;
inline constexpr size_t kLocalQuestChainMaxAlternatives = 16,
    kLocalQuestChainMaxPredicates = 16,
    kLocalQuestChainMaxPlayerConditions = 16,
    kLocalQuestChainMaxPreviousRules = 16;
struct LocalQuestChainPredicate {
    uint32_t questId = 0;
    uint8_t statusMask = 0;
};
enum class LocalQuestChainPlayerConditionKind : uint8_t {
    ItemCount = 1,
    ReputationRank = 2,
    KnownSpell = 3
};
// Player-state conditions are immutable admission metadata. `value` is the
// required item count, reputation-rank bit mask, or zero for KnownSpell.
// ItemCount alone uses includeBank; all kinds may be complemented by negated.
struct LocalQuestChainPlayerCondition {
    LocalQuestChainPlayerConditionKind kind = LocalQuestChainPlayerConditionKind::ItemCount;
    uint32_t id = 0;
    uint32_t value = 0;
    bool includeBank = false;
    bool negated = false;
};
struct LocalQuestChainAlternative {
    std::vector<LocalQuestChainPredicate> quests;
    std::vector<LocalQuestChainPlayerCondition> player;
};
// First matching predecessor decides, including an immediate rejection when
// its group requirements fail. This is not an OR of complete conjunctions.
struct LocalQuestChainPreviousRule {
    LocalQuestChainPredicate when;
    std::vector<LocalQuestChainPredicate> requirements;
};
struct LocalQuestChainGate {
    bool defined = false; // A defined gate replaces the old scalar prerequisite.
    uint8_t maxLevel = 0;
    std::vector<LocalQuestChainAlternative> alternatives;
    std::vector<LocalQuestChainPreviousRule> orderedPrevious;
    std::string unsupportedReason;
};
}
