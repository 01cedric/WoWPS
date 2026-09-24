#pragma once
#include "game/local_gameplay.hpp"

namespace wowee::game {
inline uint8_t localQuestChainStatus(const LocalRealmPlayer& player, uint32_t id) {
    if(std::binary_search(player.completedQuestIds.begin(),player.completedQuestIds.end(),id))
        return LocalQuestChainRewarded;
    for(const auto& quest:player.quests)if(quest.id==id) {
        if(quest.status==LocalQuestStatus::Active)return LocalQuestChainActive;
        if(quest.status==LocalQuestStatus::Complete)return LocalQuestChainComplete;
        if(quest.status==LocalQuestStatus::Rewarded)return LocalQuestChainRewarded;
        return 0; // Invalid state never satisfies an admission predicate.
    }
    return LocalQuestChainNone;
}
inline bool localQuestChainPlayerConditionSatisfied(const LocalRealmPlayer& player,
                                                    const LocalQuestChainPlayerCondition& condition) {
    if(!condition.id)return false;
    bool matched=false;
    switch(condition.kind) {
    case LocalQuestChainPlayerConditionKind::ItemCount: {
        if(!condition.value)return false;
        uint64_t count=0;
        for(const auto& stack:player.inventory)if(stack.itemId==condition.id)count+=stack.count;
        if(condition.includeBank)
            for(const auto& stack:player.bank)if(stack.itemId==condition.id)count+=stack.count;
        matched=count>=condition.value;
        break;
    }
    case LocalQuestChainPlayerConditionKind::ReputationRank:
        if(!condition.value || condition.value>255 || condition.includeBank)return false;
        matched=(condition.value&(1u<<localReputationRank(player,condition.id)))!=0;
        break;
    case LocalQuestChainPlayerConditionKind::KnownSpell:
        if(condition.value || condition.includeBank)return false;
        matched=std::find(player.knownSpells.begin(),player.knownSpells.end(),condition.id)!=player.knownSpells.end();
        break;
    default:return false;
    }
    return condition.negated?!matched:matched;
}
inline bool localQuestChainSatisfied(const LocalRealmPlayer& player,const LocalQuestDefinition& quest) {
    const auto& gate=quest.chainGate;
    if(!gate.defined)return !quest.prerequisite ||
        localQuestChainStatus(player,quest.prerequisite)==LocalQuestChainRewarded;
    if(!gate.unsupportedReason.empty() || gate.alternatives.empty() ||
        gate.alternatives.size()>kLocalQuestChainMaxAlternatives ||
        (gate.maxLevel && player.level>gate.maxLevel))return false;
    if(gate.orderedPrevious.size()>kLocalQuestChainMaxPreviousRules)return false;
    // Validate every row before deciding, including rows after the first match.
    for(const auto& rule:gate.orderedPrevious) {
        if(!rule.when.questId || !rule.when.statusMask || rule.when.statusMask>=15 ||
           rule.requirements.size()>kLocalQuestChainMaxPredicates)return false;
        for(const auto& predicate:rule.requirements)
            if(!predicate.questId || !predicate.statusMask || predicate.statusMask>=15)return false;
    }
    if(!gate.orderedPrevious.empty()) {
        bool found=false;
        for(const auto& rule:gate.orderedPrevious) {
            if(!(rule.when.statusMask&localQuestChainStatus(player,rule.when.questId)))continue;
            for(const auto& predicate:rule.requirements)
                if(!(predicate.statusMask&localQuestChainStatus(player,predicate.questId)))return false;
            found=true;break;
        }
        if(!found)return false;
    }
    for(const auto& alternative:gate.alternatives) {
        if(alternative.quests.size()>kLocalQuestChainMaxPredicates ||
            alternative.player.size()>kLocalQuestChainMaxPlayerConditions)return false;
        bool matched=true;
        for(const auto& predicate:alternative.quests) {
            if(!predicate.questId || !predicate.statusMask || predicate.statusMask>=15)return false;
            if(!(predicate.statusMask&localQuestChainStatus(player,predicate.questId)))matched=false;
        }
        for(const auto& condition:alternative.player)
            if(!localQuestChainPlayerConditionSatisfied(player,condition))matched=false;
        if(matched)return true;
    }
    return false;
}
// Shared by host acceptance, native/FrameXML dialogue, and world markers.
// Host interaction checks still own range, phase, NPC identity and skill-line
// support; this function only reads replicated character/immutable quest data.
inline const char* localQuestAcceptanceError(const LocalRealmPlayer& player,const LocalQuestDefinition& quest) {
    if(player.race<1 || player.race>32 || player.classId<1 || player.classId>32)
        return "Invalid character profile for this quest";
    if(quest.allowableRaces && !(quest.allowableRaces&(1u<<(player.race-1))))return "This quest is not offered to your race";
    if(quest.allowableClasses && !(quest.allowableClasses&(1u<<(player.classId-1))))return "This quest is not offered to your class";
    if(player.level<quest.minLevel)return "Level too low for this quest";
    if(quest.chainGate.defined && quest.chainGate.maxLevel && player.level>quest.chainGate.maxLevel)
        return "Level too high for this quest";
    if(quest.requiredSkill && std::none_of(player.professions.begin(),player.professions.end(),
        [&](const auto& skill){return skill.skillId==quest.requiredSkill;}))return "This quest requires a profession skill";
    if(quest.requiredMinRepFaction && localReputationStanding(player,quest.requiredMinRepFaction)<quest.requiredMinRepValue)
        return "Your reputation is too low for this quest";
    if(quest.requiredMaxRepFaction && localReputationStanding(player,quest.requiredMaxRepFaction)>=quest.requiredMaxRepValue)
        return "Your reputation is too high for this quest";
    for(size_t i=0;i<quest.requiredReputationFactions.size();++i)
        if(quest.requiredReputationFactions[i] && localReputationStanding(player,quest.requiredReputationFactions[i])<quest.requiredReputationValues[i])
            return "Your reputation is too low for this quest";
    const auto status=localQuestChainStatus(player,quest.id);
    if(status==LocalQuestChainRewarded)return "Quest reward already claimed";
    if(status!=LocalQuestChainNone)return "Quest already accepted";
    if(player.quests.size()>=LocalGameplay::MaxQuests)return "Active quest log is full (32); turn in or abandon a quest";
    if(!quest.chainGate.unsupportedReason.empty())return "This quest chain requires unsupported realm behavior";
    if(!localQuestChainSatisfied(player,quest))return "Quest chain requirements are not satisfied";
    return nullptr;
}
}
