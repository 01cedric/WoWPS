#pragma once
#include "game/local_quest_chain.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace wowee::game {
inline std::map<uint32_t,LocalQuestChainGate> parseLocalQuestChainCatalog(const nlohmann::json& json) {
    const auto integer=[](const nlohmann::json& value,uint64_t maximum) {
        if(!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>()<0) ||
            value.get<uint64_t>()>maximum)throw std::runtime_error("Invalid quest chain integer");
        return value.get<uint32_t>();
    };
    if(!json.is_object() || !json.contains("schemaVersion") || !json.contains("quests") ||
        !json.at("quests").is_array() || json.at("quests").size()>16384)
        throw std::runtime_error("Invalid quest chain companion schema");
    for(const auto& field:json.items())if(field.key()!="schemaVersion" && field.key()!="sourceCommit" &&
        field.key()!="sourceHashes" && field.key()!="quests")
        throw std::runtime_error("Unknown quest chain companion field");
    const auto schema=integer(json.at("schemaVersion"),2);
    if(schema<1)throw std::runtime_error("Invalid quest chain companion schema");
    if(json.contains("sourceCommit")) {
        if(!json.at("sourceCommit").is_string())throw std::runtime_error("Invalid quest chain source commit");
        const auto value=json.at("sourceCommit").get<std::string>();
        if(value.empty() || value.size()>128 || value.find('\0')!=std::string::npos)
            throw std::runtime_error("Invalid quest chain source commit");
    }
    if(json.contains("sourceHashes")) {
        const auto& hashes=json.at("sourceHashes");
        if(!hashes.is_object() || hashes.size()>64)throw std::runtime_error("Invalid quest chain source hashes");
        for(const auto& hash:hashes.items()) {
            if(hash.key().empty() || hash.key().size()>192 || !hash.value().is_string())
                throw std::runtime_error("Invalid quest chain source hash");
            const auto value=hash.value().get<std::string>();
            if(value.size()!=64 || value.find_first_not_of("0123456789abcdef")!=std::string::npos)
                throw std::runtime_error("Invalid quest chain source hash");
        }
    }
    std::map<uint32_t,LocalQuestChainGate> result;
    for(const auto& row:json.at("quests")) {
        if(!row.is_object() || !row.contains("id") || !row.contains("alternatives"))
            throw std::runtime_error("Incomplete quest chain row");
        for(const auto& field:row.items())if(field.key()!="id" && field.key()!="maxLevel" &&
            field.key()!="alternatives" && field.key()!="unsupportedReason" && field.key()!="orderedPrevious")
            throw std::runtime_error("Unknown quest chain gate field");
        const auto id=integer(row.at("id"),UINT32_MAX);
        if(!id || result.count(id))throw std::runtime_error("Duplicate or zero quest chain ID");
        LocalQuestChainGate gate;gate.defined=true;
        if(row.contains("maxLevel"))gate.maxLevel=uint8_t(integer(row.at("maxLevel"),80));
        if(row.contains("unsupportedReason")) {
            if(!row.at("unsupportedReason").is_string())throw std::runtime_error("Invalid quest chain unsupported reason");
            gate.unsupportedReason=row.at("unsupportedReason").get<std::string>();
            if(gate.unsupportedReason.size()>1024 || gate.unsupportedReason.find('\0')!=std::string::npos)
                throw std::runtime_error("Invalid quest chain unsupported reason");
        }
        const auto& alternatives=row.at("alternatives");
        if(!alternatives.is_array() || alternatives.size()>kLocalQuestChainMaxAlternatives ||
            alternatives.empty()!=!gate.unsupportedReason.empty())
            throw std::runtime_error("Invalid or unlabelled blocked quest chain alternatives");
        if(row.contains("orderedPrevious")) {
            const auto& rules=row.at("orderedPrevious");
            if(schema<2 || !rules.is_array() || rules.empty() || rules.size()>kLocalQuestChainMaxPreviousRules ||
               !gate.unsupportedReason.empty())throw std::runtime_error("Invalid ordered quest predecessor rules");
            std::set<uint32_t> sources;
            const auto readPredicate=[&](const nlohmann::json& predicate) {
                if(!predicate.is_object() || predicate.size()!=2 || !predicate.contains("questId") ||
                   !predicate.contains("statusMask"))throw std::runtime_error("Invalid ordered quest predicate");
                const auto questId=integer(predicate.at("questId"),UINT32_MAX);
                const auto mask=integer(predicate.at("statusMask"),14);
                if(!questId || !mask)throw std::runtime_error("Invalid ordered quest predicate");
                return LocalQuestChainPredicate{questId,uint8_t(mask)};
            };
            for(const auto& rule:rules) {
                if(!rule.is_object() || rule.size()!=2 || !rule.contains("when") || !rule.contains("require") ||
                   !rule.at("require").is_array() || rule.at("require").size()>kLocalQuestChainMaxPredicates)
                    throw std::runtime_error("Invalid ordered quest predecessor rule");
                LocalQuestChainPreviousRule output;output.when=readPredicate(rule.at("when"));
                if(!sources.insert(output.when.questId).second)throw std::runtime_error("Duplicate ordered quest predecessor");
                std::set<uint32_t> references{output.when.questId};
                for(const auto& predicate:rule.at("require")) {
                    const auto requirement=readPredicate(predicate);
                    if(!references.insert(requirement.questId).second)throw std::runtime_error("Duplicate ordered quest requirement");
                    output.requirements.push_back(requirement);
                }
                gate.orderedPrevious.push_back(std::move(output));
            }
        }
        std::set<std::string> uniqueAlternatives;
        for(const auto& alternative:alternatives) {
            if(!alternative.is_array() || alternative.size()>kLocalQuestChainMaxPredicates+kLocalQuestChainMaxPlayerConditions ||
                (alternative.empty() && alternatives.size()!=1))throw std::runtime_error("Invalid quest chain conjunction");
            LocalQuestChainAlternative output;
            std::set<uint32_t> seenQuests;
            std::set<std::tuple<unsigned,uint32_t,uint32_t,bool,bool>> seenPlayer;
            for(const auto& predicate:alternative) {
                if(!predicate.is_object())throw std::runtime_error("Invalid quest chain predicate");
                if(predicate.contains("questId")) {
                    if(predicate.size()!=2 || !predicate.contains("statusMask"))
                        throw std::runtime_error("Invalid quest chain predicate");
                    const auto questId=integer(predicate.at("questId"),UINT32_MAX);
                    const auto mask=integer(predicate.at("statusMask"),14);
                    if(!questId || !mask || !seenQuests.insert(questId).second)
                        throw std::runtime_error("Invalid or duplicate quest chain predicate");
                    output.quests.push_back({questId,uint8_t(mask)});
                    continue;
                }
                if(schema<2)throw std::runtime_error("Player condition requires quest chain schema 2");
                LocalQuestChainPlayerCondition condition;
                if(predicate.contains("itemId")) {
                    if(predicate.size()!=4 || !predicate.contains("count") || !predicate.contains("includeBank") ||
                        !predicate.contains("negated") || !predicate.at("includeBank").is_boolean() ||
                        !predicate.at("negated").is_boolean())throw std::runtime_error("Invalid item quest condition");
                    condition.kind=LocalQuestChainPlayerConditionKind::ItemCount;
                    condition.id=integer(predicate.at("itemId"),UINT32_MAX);
                    condition.value=integer(predicate.at("count"),UINT32_MAX);
                    condition.includeBank=predicate.at("includeBank").get<bool>();
                    condition.negated=predicate.at("negated").get<bool>();
                    if(!condition.id || !condition.value)throw std::runtime_error("Invalid item quest condition");
                } else if(predicate.contains("factionId")) {
                    if(predicate.size()!=3 || !predicate.contains("rankMask") || !predicate.contains("negated") ||
                        !predicate.at("negated").is_boolean())throw std::runtime_error("Invalid reputation quest condition");
                    condition.kind=LocalQuestChainPlayerConditionKind::ReputationRank;
                    condition.id=integer(predicate.at("factionId"),UINT32_MAX);
                    condition.value=integer(predicate.at("rankMask"),255);
                    condition.negated=predicate.at("negated").get<bool>();
                    if(!condition.id || !condition.value)throw std::runtime_error("Invalid reputation quest condition");
                } else if(predicate.contains("spellId")) {
                    if(predicate.size()!=2 || !predicate.contains("negated") || !predicate.at("negated").is_boolean())
                        throw std::runtime_error("Invalid spell quest condition");
                    condition.kind=LocalQuestChainPlayerConditionKind::KnownSpell;
                    condition.id=integer(predicate.at("spellId"),UINT32_MAX);
                    condition.negated=predicate.at("negated").get<bool>();
                    if(!condition.id)throw std::runtime_error("Invalid spell quest condition");
                } else throw std::runtime_error("Unknown quest chain predicate");
                const auto key=std::make_tuple(unsigned(condition.kind),condition.id,condition.value,
                                               condition.includeBank,condition.negated);
                if(!seenPlayer.insert(key).second)throw std::runtime_error("Duplicate player quest condition");
                const auto complement=std::make_tuple(unsigned(condition.kind),condition.id,condition.value,
                                                      condition.includeBank,!condition.negated);
                if(seenPlayer.count(complement))throw std::runtime_error("Contradictory player quest condition");
                output.player.push_back(condition);
            }
            if(output.quests.size()>kLocalQuestChainMaxPredicates || output.player.size()>kLocalQuestChainMaxPlayerConditions)
                throw std::runtime_error("Quest chain conjunction exceeds runtime bounds");
            std::sort(output.quests.begin(),output.quests.end(),[](const auto& a,const auto& b){return a.questId<b.questId;});
            std::sort(output.player.begin(),output.player.end(),[](const auto& a,const auto& b){
                return std::make_tuple(unsigned(a.kind),a.id,a.value,a.includeBank,a.negated)<
                       std::make_tuple(unsigned(b.kind),b.id,b.value,b.includeBank,b.negated);
            });
            std::string normalized;
            for(const auto& q:output.quests)normalized+="q:"+std::to_string(q.questId)+":"+std::to_string(q.statusMask)+";";
            for(const auto& p:output.player)normalized+="p:"+std::to_string(unsigned(p.kind))+":"+std::to_string(p.id)+":"+
                std::to_string(p.value)+":"+(p.includeBank?"1":"0")+":"+(p.negated?"1":"0")+";";
            if(!uniqueAlternatives.insert(normalized).second)throw std::runtime_error("Duplicate quest chain alternative");
            gate.alternatives.push_back(std::move(output));
        }
        result.emplace(id,std::move(gate));
    }
    return result;
}
}
